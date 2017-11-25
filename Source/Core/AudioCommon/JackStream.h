// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "AudioCommon/SoundStream.h"
#include <jack/jack.h>

class JackStream final : public SoundStream
{
public:
  bool Start() override;
  void Stop() override;
  void SetVolume(int) override;
  static bool isValid()
  {
    return true;
  }

private:
  jack_client_t* m_client = nullptr;
  bool m_stereo = false;
  std::vector<jack_port_t*> m_ports;
  float m_volume = 1.0f;

  static int ProcessCallback (jack_nframes_t nframes, void* arg);
};
