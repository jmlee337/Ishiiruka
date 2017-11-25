// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>

#include "AudioCommon/JackStream.h"
#include "Common/Logging/Log.h"

constexpr const char* CLIENT_NAME = "Ishiiruka";
constexpr const char* PORT_NAMES[6] = {"ch1", "ch2", "ch3", "ch4", "ch5", "ch6"};
constexpr const jack_default_audio_sample_t DIVISOR = pow(2, sizeof(short) * 8 - 1);

int JackStream::ProcessCallback(jack_nframes_t nframes, void* arg)
{
  auto* self = static_cast<JackStream*>(arg);
  if (self->m_stereo)
  {
    short buf[nframes * 2];
    if (self->m_mixer->Mix(buf, nframes) == 0)
      return -1;

    jack_default_audio_sample_t* buffer_l =
        (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[0], nframes);
    jack_default_audio_sample_t* buffer_r =
        (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[1], nframes);
    for (unsigned int i = 0; i < nframes; i++)
    {
      buffer_l[i] = buf[2 * i] * self->m_volume / DIVISOR;
      buffer_r[i] = buf[2 * i + 1] * self->m_volume / DIVISOR;
    }
  }
  else
  {
    float buf[nframes * 6];
    if (self->m_mixer->MixSurround(buf, nframes) == 0)
      return -1;

    for (int i = 0; i < 6; i++)
    {
      jack_default_audio_sample_t* buffer =
          (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[i], nframes);
      std::copy(buf + (i * nframes), buf + ((i + 1) * nframes), buffer);
    }
  }
  return 0;
}

bool JackStream::Start()
{
  m_client = jack_client_open(CLIENT_NAME, JackNullOption, nullptr);
  if (m_client == nullptr)
  {
    ERROR_LOG(AUDIO, "Error opening jack client");
    return false;
  }
  if (jack_set_process_callback(m_client, ProcessCallback, this))
  {
    ERROR_LOG(AUDIO, "Error setting jack client callback");
    return false;
  }

  // Unfortunately it's not possible to control the sample rate of the JACK server from a client.
  // However we can adjust our own sample rate to match. This won't always produce good results, so
  // log a warning if we adjust.
  jack_nframes_t actual_sample_rate = jack_get_sample_rate(m_client);
  unsigned int default_sample_rate = m_mixer->GetSampleRate();
  if (actual_sample_rate != default_sample_rate)
  {
    m_mixer.reset(new Mixer(actual_sample_rate));
    if (actual_sample_rate > default_sample_rate)
    {
      WARN_LOG(
          AUDIO,
          "Default sample rate: %u raised to match jack server: %u. This could cause errors.",
          default_sample_rate,
          actual_sample_rate);
    }
    else
    {
      WARN_LOG(
          AUDIO,
          "Default sample rate: %u lowered to match jack server: %u. This will increase latency.",
          default_sample_rate,
          actual_sample_rate);
    }
  }

  if (jack_activate(m_client))
  {
    ERROR_LOG(AUDIO, "Error activating jack client");
    return false;
  }

  // from this point on, we have to free ports before leaving this context.
  int ports_wanted = m_stereo ? 2 : 6;
  const char** ports = jack_get_ports(m_client, nullptr, nullptr, JackPortIsPhysical|JackPortIsInput);
  if (!ports)
  {
    ERROR_LOG(AUDIO, "No physical playback ports for jack");
    return false;
  }
  int ports_count = 0;
  while (ports[ports_count])
    ports_count++;
  if (ports_count < ports_wanted)
  {
    ERROR_LOG(AUDIO, "Not enough physical output ports: %d, wanted %d", ports_count, ports_wanted);
    jack_free(ports);
    return false;
  }
  for (int i = 0; i < ports_wanted; i++)
  {
    jack_port_t* port =
        jack_port_register(m_client, PORT_NAMES[i], JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (port == nullptr)
    {
      ERROR_LOG(AUDIO, "Error registering jack output port: %s", PORT_NAMES[i]);
      jack_free(ports);
      return false;
    }
    m_ports.push_back(port);

    if (jack_connect(m_client, jack_port_name(port), ports[i]))
    {
      ERROR_LOG(AUDIO, "Error connecting to phsyical playback port: %s", PORT_NAMES[i]);
      jack_free(ports);
      return false;
    }
  }
  jack_free(ports);
  return true;
}

void JackStream::Stop()
{
  if (jack_deactivate(m_client))
  {
    ERROR_LOG(AUDIO, "Error deactivating jack client");
  }
  if (jack_client_close(m_client))
  {
    ERROR_LOG(AUDIO, "Error closing jack client");
  }
  m_client = nullptr;
  m_ports.clear();
}

void JackStream::SetVolume(int volume)
{
  m_volume = volume / 100.0f;
}
