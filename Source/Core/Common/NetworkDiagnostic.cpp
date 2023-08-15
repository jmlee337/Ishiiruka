#include "Common/NetworkDiagnostic.h"

#include "CommonTypes.h"
#include "Logging/Log.h"
#include <enet/enet.h>
#include <ws2ipdef.h>
#include <random>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

static const std::string STUN_HOST_1 = "stun1.l.google.com";
static const std::string STUN_HOST_2 = "stun2.l.google.com";
static const enet_uint16 STUN_PORT_1 = 19302;
static const enet_uint16 STUN_PORT_2 = 19302;
static const enet_uint16 ICMP_PORT = 7;

static wxTextCtrl *s_textCtrl = nullptr;
static std::thread s_thread;

static void MakeStunRequest(u8 *out)
{
	// messageType
	out[0] = 0x00;
	out[1] = 0x01;

	// messageLength
	out[2] = 0x00;
	out[3] = 0x00;

	// magicCookie
	out[4] = 0x21;
	out[5] = 0x12;
	out[6] = 0xA4;
	out[7] = 0x42;

	std::random_device randomDevice;
	std::uniform_int_distribution<> u8Dist(0x00, 0xFF);
	// (8 - 19) transactionId
	for (int i = 8; i < 20; i++)
		out[i] = u8Dist(randomDevice);

	for (int i = 0; i < 20; i += 4)
		INFO_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN req: %02X%02X%02X%02X", out[i], out[i + 1], out[i + 2], out[i + 3]);
}

static bool Stun(ENetSocket socket, ENetAddress stunAddress, u8 *out)
{
	int ret = 0;
	u8 stunRequest[20];
	MakeStunRequest(stunRequest);
	ENetBuffer enetBufferOut;
	enetBufferOut.data = &stunRequest;
	enetBufferOut.dataLength = sizeof(stunRequest);
	ENetBuffer enetBufferIn;
	enetBufferIn.data = out;
	enetBufferIn.dataLength = 64;
	for (int timeout = 500; timeout <= 2000; timeout *= 2)
	{
		ret = enet_socket_send(socket, &stunAddress, &enetBufferOut, 1);
		if (ret <= 0)
		{
			ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: STUN send: %d", ret);
			return false;
		}

		enet_socket_set_option(socket, ENET_SOCKOPT_RCVTIMEO, timeout);
		ret = enet_socket_receive(socket, nullptr, &enetBufferIn, 1);
		if (ret > 0)
			break;

		ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: STUN receive: %d", ret);
	}
	return ret > 0;
}

static ENetAddress ProcessStunResponse(u8 *in)
{
	ENetAddress mappedAddress = {0, 0};
	if (in[4] != 0x21 || in[5] != 0x12 || in[6] != 0xA4 || in[7] != 0x42)
	{
		ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: not a STUN response");
		return mappedAddress;
	}

	u16 messageLength = in[2];
	messageLength = messageLength << 8;
	messageLength += in[3];
	for (u16 i = 0; i < messageLength + 20; i += 4)
		INFO_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp: %02X%02X%02X%02X", in[i], in[i + 1], in[i + 2],
		         in[i + 3]);
	INFO_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp message length: %d", messageLength);
	if (in[0] != 0x01 || in[1] != 0x01)
	{
		ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: unexpected STUN response");
		return mappedAddress;
	}

	u16 i = 20;
	while (i < messageLength + 20)
	{
		u16 attributeType = in[i];
		attributeType = attributeType << 8;
		attributeType += in[i + 1];
		u16 attributeLength = in[i + 2];
		attributeLength = attributeLength << 8;
		attributeLength += in[i + 3];
		INFO_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp attribute type: 0x%02X, length: %d", attributeType,
		         attributeLength);

		switch (attributeType)
		{
		case 0x01:
		{
			if (in[i + 5] != 0x01)
			{
				ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp MAPPED-ADDRESS family not IPv4: %d",
				          in[i + 5]);
				break;
			}

			enet_uint16 port = in[i + 6];
			port = port << 8;
			port += in[i + 7];

			enet_uint32 host = in[i + 8];
			host = host << 8;
			host += in[i + 9];
			host = host << 8;
			host += in[i + 10];
			host = host << 8;
			host += in[i + 11];

			WARN_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp MAPPED-ADDRESS %s:%d", inet_ntoa(*(in_addr *)&host),
			         port);
			mappedAddress.host = host;
			mappedAddress.port = port;
		}
		case 0x20:
		{
			if (in[i + 5] != 0x01)
			{
				ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp XOR-MAPPED-ADDRESS family not IPv4: %d",
				          in[i + 5]);
				break;
			}

			enet_uint16 port = (in[i + 7] ^ 0x42);
			port = port << 8;
			port += (in[i + 6] ^ 0xA4);

			enet_uint32 host = (in[i + 11] ^ 0x42);
			host = host << 8;
			host += (in[i + 10] ^ 0xA4);
			host = host << 8;
			host += (in[i + 9] ^ 0x12);
			host = host << 8;
			host += (in[i + 8] ^ 0x21);

			WARN_LOG(SLIPPI_ONLINE, "[Network Diagnostic] STUN resp XOR-MAPPED-ADDRESS %s:%d", inet_ntoa(*(in_addr *)&host),
			         port);
			mappedAddress.host = host;
			mappedAddress.port = port;
		}
		}

		i += attributeLength + 4;
	}

	return mappedAddress;
}

// NAT Type test
// To determine NAT type, send two identical requests from the same port asking two different STUN servers what our
// external IP/port is.If they don't match, then we have a hard/strict/symmetric NAT.
static enet_uint32 NatTypeTest()
{
	ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
	if (socket <= 0)
	{
		*s_textCtrl << "NAT type test failed! could not create socket";
		return;
	}

	ENetAddress stunAddress1;
	enet_address_set_host(&stunAddress1, STUN_HOST_1.c_str());
	stunAddress1.port = STUN_PORT_1;
	u8 out1[64] = {0};
	if (!Stun(socket, stunAddress1, out1))
	{
		*s_textCtrl << "NAT type test failed! STUN request 1\n";
		return 0;
	}
	ENetAddress address1 = ProcessStunResponse(out1);
	if (address1.host == ENET_HOST_ANY)
	{
		*s_textCtrl << "NAT type test failed! STUN response 1 invalid\n";
		return 0;
	}

	ENetAddress stunAddress2;
	enet_address_set_host(&stunAddress2, STUN_HOST_2.c_str());
	stunAddress2.port = STUN_PORT_2;
	u8 out2[64] = {0};
	if (!Stun(socket, stunAddress2, out2))
	{
		*s_textCtrl << "NAT type test failed! STUN request 2\n";
		return address1.host;
	}
	ENetAddress address2 = ProcessStunResponse(out2);
	if (address2.host == ENET_HOST_ANY)
	{
		*s_textCtrl << "NAT type test failed! STUN reponse 2 invalid\n";
		return address1.host;
	}

	enet_socket_destroy(socket);
	*s_textCtrl << "NAT type: ";
	if (address1.host == address2.host && address1.port == address2.port)
		*s_textCtrl << "Normal\n";
	else
		*s_textCtrl << "Symmetric\n";
	return address1.host;
}

static int Ping(int socket, int ttl, enet_uint32 inHost, u_long *outHost)
{
	u32 bufOut[1];
	ENetBuffer outBuffer;
	outBuffer.data = bufOut;
	outBuffer.dataLength = 0;

	struct sockaddr_in pingAddress;
	pingAddress.sin_family = AF_INET;
	pingAddress.sin_addr.s_addr = inHost;
	pingAddress.sin_port = htons(ICMP_PORT);

	u8 bufIn[64];
	ENetBuffer inBuffer;
	inBuffer.data = bufIn;
	inBuffer.dataLength = sizeof(bufIn);

	ENetAddress inAddress = {0, 0};
	for (int timeout = 500; timeout <= 2000; timeout *= 2)
	{
		int ret = sendto(socket, bufOut, sizeof(bufOut), 0, (sockaddr *)&pingAddress, sizeof(pingAddress));
		if (ret != 0)
		{
			ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: ping send: %d", ret);
			return 0;
		}

		enet_socket_set_option(socket, ENET_SOCKOPT_RCVTIMEO, timeout);
		ret = enet_socket_receive(socket, &inAddress, &inBuffer, 1);
		if (ret >= 0)
			break;

		ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Failed: ping receive: %d", ret);
	}

	if (inAddress.host != 0)
	{
		*outHost = inAddress.host;
		return 1;
	}
	return 0;
}

// CGNAT/Double NAT test
// Traceroute to our own external IP, any intermediate hops indicates CGNAT/double NAT.
static bool DoubleNatTest(enet_uint32 ownHost)
{
	int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sock <= 0)
	{
		*s_textCtrl << "CGNAT/Double NAT test failed! could not create raw socket\n";
		return false;
	}
	int val = 1;
	int ret = setsockopt(sock, IPPROTO_IP, IP_HDRINCL, (char *)&val, sizeof(val));
	if (ret != 0)
	{
		*s_textCtrl << "CGNAT/Double NAT test failed! could not set IP header\n";
		return false;
	}

	std::vector<u_long> route;
	for (int i = 1; i < 8; i++)
	{
		u_long host = 0;
		int pingRet = Ping(sock, i, ownHost, &host);
		if (pingRet < 0)
		{
			return;
		}
		else if (pingRet > 0)
		{
			if (route.size() > 0 && host != *route.end())
				route.push_back(host);
			if (host == ownHost)
				break;
		}
	}
	if (route.size() == 0)
	{
		*s_textCtrl << "CGNAT/Double NAT test failed! Traceroute found no hosts\n";
		return;
	}
	for (auto host : route)
		INFO_LOG(SLIPPI_ONLINE, "[Network Diagnostic] Traceroute: %s", inet_ntoa(*(in_addr *)&host));

	*s_textCtrl << "CGNAT/Double NAT: ";
	if (route.size() == 1)
		*s_textCtrl << "Not detected\n";
	else
		*s_textCtrl << "Detected\n";
}

static void NetworkDiagnosticThread()
{
	*s_textCtrl << "Starting...\n\n";

	enet_uint32 ownHost = NatTypeTest();
	DoubleNatTest(ownHost);

	*s_textCtrl << "\nDone!";
}

namespace NetworkDiagnostic
{
void SetTextCtrl(wxTextCtrl *textCtrl)
{
	s_textCtrl = textCtrl;
}

bool Start()
{
	if (s_textCtrl == nullptr)
	{
		ERROR_LOG(SLIPPI_ONLINE, "[Network Diagnostic] TextCtrl unset");
		return false;
	}

	if (s_thread.joinable())
		s_thread.join();
	s_thread = std::thread(&NetworkDiagnosticThread);
	return true;
}

void Stop()
{
	if (s_thread.joinable())
		s_thread.join();
}
} // namespace NetworkDiagnostic
