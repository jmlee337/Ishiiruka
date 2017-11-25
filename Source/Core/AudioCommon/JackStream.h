// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "AudioCommon/SoundStream.h"
#include "Core/ConfigManager.h"

#if defined(HAVE_JACK) && HAVE_JACK
#include <jack/jack.h>
#endif

class JackStream final : public SoundStream
{
#if defined(HAVE_JACK) && HAVE_JACK
public:
  bool Start() override;
  void Stop() override;
  void SetVolume(int) override;
  static bool isValid()
  {
    return true;
  }

private:
  bool m_stereo = !SConfig::GetInstance().bDPL2Decoder;
  float m_volume = 0.0f;
  jack_client_t* m_client = nullptr;
  std::vector<jack_port_t*> m_ports;

  static int ProcessCallback(jack_nframes_t nframes, void* arg);
#endif
};
