// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>

#include "AudioCommon/JackStream.h"
#include "Common/Logging/Log.h"

constexpr const char* CLIENT_NAME = "Ishiiruka";
constexpr const char* PORT_NAMES[6] = {"ch1", "ch2", "ch3", "ch4", "ch5", "ch6"};
constexpr const float DIVISOR = static_cast<float>(std::numeric_limits<short>::max());

int JackStream::ProcessCallback(jack_nframes_t nframes, void* arg)
{
  JackStream* self = static_cast<JackStream*>(arg);
  if (self->m_stereo)
  {
    short buf[nframes * 2];
    if (self->m_mixer->Mix(buf, nframes) == 0)
      return -1;

    jack_default_audio_sample_t* buffer_l =
        (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[0], nframes);
    jack_default_audio_sample_t* buffer_r =
        (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[1], nframes);

    // this method of de-interleaving is about 4x faster than a naive array iteration when compiling
    // with -O3 on x86-64.
    struct
    {
      short l;
      short r;
    }* mixed = reinterpret_cast<decltype(mixed)>(buf);
    for (unsigned int i = 0; i < nframes; i++)
    {
      // normalize range of s16 to [-1.0, 1.0]
      *(buffer_l++) = mixed->l * self->m_volume / DIVISOR;
      *(buffer_r++) = mixed->r * self->m_volume / DIVISOR;
      mixed++;
    }
  }
  else
  {
    float buf[nframes * 6];
    if (self->m_mixer->MixSurround(buf, nframes) == 0)
      return -1;

    jack_default_audio_sample_t* out_bufs[6];
    for (unsigned int i = 0; i < 6; i++)
    {
      out_bufs[i] = (jack_default_audio_sample_t*)jack_port_get_buffer(self->m_ports[i], nframes);
    }

    // Again, this is really fast when compiled optimized compared with a naive array iteration.
    struct
    {
      float f0;
      float f1;
      float f2;
      float f3;
      float f4;
      float f5;
    }* mixed = reinterpret_cast<decltype(mixed)>(buf);
    for (unsigned int i = 0; i < nframes; i++)
    {
      // 0 leftfront, 1 rightfront, 2 center, 3 sub, 4 leftrear, 5 rightrear
      // ignore the sub channel. According to comments in the other backends, it's no good.
      *(out_bufs[0]++) = mixed->f0 * self->m_volume;
      *(out_bufs[1]++) = mixed->f1 * self->m_volume;
      *(out_bufs[2]++) = mixed->f2 * self->m_volume;
      *(out_bufs[4]++) = mixed->f4 * self->m_volume;
      *(out_bufs[5]++) = mixed->f5 * self->m_volume;
    }
    // zero the sub channel.
    std::memset(out_bufs[3], 0, sizeof(float) * nframes);
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
