//
// minidexed.cpp
//
// Mini-JV880pi - Roland JV880 synthesizer for bare metal Raspberry Pi
// Copyright (C) 2022  The MiniDexed Team
// Copyright (C) 2026  Plamikcho, Giulioz, Gene J.B. (Sterr1)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "minijv880.h" 
#include "midi.h"
#include "userinterface.h"
#include <assert.h>
#include <circle/memory.h>
#include <circle/devicenameservice.h>
#include <circle/gpiopin.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/usb/usbmidihost.h>
#include <circle/net/syslogdaemon.h>
#include <circle/net/ipaddress.h>
#include <stdio.h>
#include <string.h>
#include <cstring>   // memcpy / memmove
#include <algorithm>

const char WLANFirmwarePath[] = "SD:firmware/";
const char WLANConfigFile[]   = "SD:wpa_supplicant.conf";
#define FTPUSERNAME "admin"
#define FTPPASSWORD "admin"

LOGMODULE("minijv880");

CMiniJV880 *CMiniJV880::s_pThis = 0;

uint16_t cnt = 0;

CMiniJV880::RomInfo CMiniJV880::m_romInfos[27] = {
    m_romInfos[0] = {sz32K, "jv880_nvram.bin", false, false, false, nullptr},
    m_romInfos[1] = {sz32K, "jv880_rom1.bin", false, false, false, nullptr},
    m_romInfos[2] = {sz256K, "jv880_rom2.bin", false, false, false, nullptr},
    m_romInfos[3] = {sz2M, "jv880_waverom1.bin", true, false, true, nullptr},
    m_romInfos[4] = {sz2M, "jv880_waverom2.bin", true, false, true, nullptr},
    m_romInfos[5] = {sz128K, "rd500_patches.bin", false, false, false, nullptr},
    m_romInfos[6] = {sz8M, "rd500_expansion.bin", true, false, true, nullptr},
    m_romInfos[7] = {sz8M, "SR-JV80-01 Pop - CS 0x3F1CF705.bin", true, false, true, nullptr},
    m_romInfos[8] = {sz8M, "SR-JV80-02 Orchestral - CS 0x3F0E09E2.BIN", true, false, true, nullptr},
    m_romInfos[9] = {sz8M, "SR-JV80-03 Piano - CS 0x3F8DB303.bin", true, false, true, nullptr},
    m_romInfos[10] = {sz8M, "SR-JV80-04 Vintage Synth - CS 0x3E23B90C.BIN", true, false, true, nullptr},
    m_romInfos[11] = {sz8M, "SR-JV80-05 World - CS 0x3E8E8A0D.bin", true, false, true, nullptr},
    m_romInfos[12] = {sz8M, "SR-JV80-06 Dance - CS 0x3EC462E0.bin", true, false, true, nullptr},
    m_romInfos[13] = {sz8M, "SR-JV80-07 Super Sound Set - CS 0x3F1EE208.bin", true, false, true, nullptr},
    m_romInfos[14] = {sz8M, "SR-JV80-08 Keyboards of the 60s and 70s - CS 0x3F1E3F0A.BIN", true, false, true, nullptr},
    m_romInfos[15] = {sz8M, "SR-JV80-09 Session - CS 0x3F381791.BIN", true, false, true, nullptr},
    m_romInfos[16] = {sz8M, "SR-JV80-10 Bass & Drum - CS 0x3D83D02A.BIN", true, false, true, nullptr},
    m_romInfos[17] = {sz8M, "SR-JV80-11 Techno - CS 0x3F046250.bin", true, false, true, nullptr},
    m_romInfos[18] = {sz8M, "SR-JV80-12 HipHop - CS 0x3EA08A19.BIN", true, false, true, nullptr},
    m_romInfos[19] = {sz8M, "SR-JV80-13 Vocal - CS 0x3ECE78AA.bin", true, false, true, nullptr},
    m_romInfos[20] = {sz8M, "SR-JV80-14 Asia - CS 0x3C8A1582.bin", true, false, true, nullptr},
    m_romInfos[21] = {sz8M, "SR-JV80-15 Special FX - CS 0x3F591CE4.bin", true, false, true, nullptr},
    m_romInfos[22] = {sz8M, "SR-JV80-16 Orchestral II - CS 0x3F35B03B.bin", true, false, true, nullptr},
    m_romInfos[23] = {sz8M, "SR-JV80-17 Country - CS 0x3ED75089.bin", true, false, true, nullptr},
    m_romInfos[24] = {sz8M, "SR-JV80-18 Latin - CS 0x3EA51033.BIN", true, false, true, nullptr},
    m_romInfos[25] = {sz8M, "SR-JV80-19 House - CS 0x3E330C41.BIN", true, false, true, nullptr},
    m_romInfos[26] = {sz8M, "rd500_expansion.bin", true, false, true, nullptr}
};


CMiniJV880::CMiniJV880(CConfig *pConfig, CInterruptSystem *pInterrupt,
                       CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster,
                       FATFS *pFileSystem, CScreenDevice *mScreenUnbuffered,
                       CWriteBufferDevice *pHDMIScreen)
    : CMultiCoreSupport(CMemorySystem::Get()), m_pConfig(pConfig),
      m_pFileSystem(pFileSystem), 
      m_Serial(pInterrupt, TRUE),
      m_pSoundDevice(0),
      screenUnbuffered(mScreenUnbuffered),
      m_bChannelsSwapped(pConfig->GetChannelsSwapped()),
      m_UI(this, pGPIOManager, pI2CMaster, pSPIMaster, pConfig, mScreenUnbuffered, pHDMIScreen),
      m_pNet(nullptr),
        m_pNetDevice(nullptr),
        m_WLAN(nullptr),
        m_WPASupplicant(nullptr),
        m_bNetworkReady(false),
        m_bNetworkInit(false),
        m_UDPMIDI(nullptr),
        m_pmDNSPublisher (nullptr),
      m_lastTick(0),
      m_lastTick1(0) {


      CTimer::Get();
  
      assert(m_pConfig);

      s_pThis = this;

      __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELAXED);
      m_nPendingBankSwitch.store(0xFF, std::memory_order_release);

      m_bMIDISurfaceEnabled = pConfig->GetMIDISurfaceEnabled();
      m_nMIDISurfaceChannel = pConfig->GetMIDISurfaceCh();
      m_nMIDISurfaceDeviceID = pConfig->GetMIDISurfaceDeviceID() & 0x7F;
      m_bMIDISurfaceLEDFeedback = pConfig->GetMIDISurfaceLEDFeedback();
      m_nMIDISurfaceTemplate = pConfig->GetMIDISurfaceTemplate() & 0x0F;
      // Internal row order is top, middle, bottom, fader.
      m_nMIDISurfaceCCStart[0] = pConfig->GetMIDISurfacePotRow1CCStart() & 0x7F;
      m_nMIDISurfaceCCStart[1] = pConfig->GetMIDISurfacePotRow2CCStart() & 0x7F;
      m_nMIDISurfaceCCStart[2] = pConfig->GetMIDISurfacePotRow3CCStart() & 0x7F;
      m_nMIDISurfaceCCStart[3] = pConfig->GetMIDISurfaceFadersCCStart() & 0x7F;
      m_bMIDISurfacePickupEnabled = pConfig->GetMIDISurfacePickupEnabled();
      m_nMIDISurfacePickupRange = pConfig->GetMIDISurfacePickupRange();
      for (unsigned i = 0; i < 16; ++i) {
          m_nMIDISurfaceButtons[i] = pConfig->GetMIDISurfaceButton(i + 1) & 0x7F;
          m_nMIDISurfaceLastLED[i] = 0xFF;
      }
      m_currentBankNumber = -1;
      m_currentExpansionRomIndex = pConfig->GetExpRom() == 0
          ? -1 : (int)pConfig->GetExpRom() + 6;
      for (unsigned tone = 0; tone < 4; ++tone) {
          m_SurfaceToneValidMask[tone].store(0, std::memory_order_relaxed);
          for (unsigned parameter = 0; parameter < SurfaceToneParameterCount; ++parameter)
              m_SurfaceToneValues[tone][parameter].store(0, std::memory_order_relaxed);
      }
      for (unsigned parameter = 0; parameter < SurfaceCommonParameterCount; ++parameter)
          m_SurfaceCommonValues[parameter].store(0, std::memory_order_relaxed);
      for (unsigned part = 0; part < 8; ++part) {
          m_SurfacePartEnabled[part].store(0, std::memory_order_relaxed);
          m_SurfacePartLevel[part].store(0, std::memory_order_relaxed);
          m_SurfacePartPan[part].store(0, std::memory_order_relaxed);
          m_SurfacePartFieldMask[part].store(0, std::memory_order_relaxed);
      }
      ResetMIDISurfacePickup();

  // select the sound device
  const char *pDeviceName = pConfig->GetSoundDevice();
  if (strcmp(pDeviceName, "i2s") == 0) {
    LOGNOTE("I2S mode");
    m_pSoundDevice = new CI2SSoundBaseDevice(
        pInterrupt, pConfig->GetSampleRate(), pConfig->GetChunkSize(), false, pI2CMaster,
        pConfig->GetDACI2CAddress(), CI2SSoundBaseDevice::DeviceModeTXOnly,
        2); // 2 channels - L+R
  } else if (strcmp(pDeviceName, "hdmi") == 0) {
#if RASPPI == 5
    LOGNOTE("HDMI mode NOT supported on RPI 5.");
#else
    LOGNOTE("HDMI mode");

    m_pSoundDevice =
        new CHDMISoundBaseDevice(pInterrupt, pConfig->GetSampleRate(), pConfig->GetChunkSize());

    // The channels are swapped by default in the HDMI sound driver.
    // TODO: Remove this line, when this has been fixed in the driver.
    m_bChannelsSwapped = !m_bChannelsSwapped;
#endif
  } else {
    LOGNOTE("PWM mode");

    m_pSoundDevice =
        new CPWMSoundBaseDevice(pInterrupt, pConfig->GetSampleRate(), pConfig->GetChunkSize());
  }
  
};

CMiniJV880::~CMiniJV880 ()
{
	// Cleanup network services (in reverse order of creation)
    if (m_pmDNSPublisher) {
        delete m_pmDNSPublisher;
        m_pmDNSPublisher = nullptr;
    }
    
    if (m_pFTPDaemon) {
        delete m_pFTPDaemon;
        m_pFTPDaemon = nullptr;
    }
    
    if (m_UDPMIDI) {
        delete m_UDPMIDI;
        m_UDPMIDI = nullptr;
    }
    
    if (m_WPASupplicant) {
        delete m_WPASupplicant;
        m_WPASupplicant = nullptr;
    }
    
    if (m_WLAN) {
        delete m_WLAN;
        m_WLAN = nullptr;
    }
    
    if (m_pNet) {
        delete m_pNet;
        m_pNet = nullptr;
    }
}

bool CMiniJV880::Initialize(void) {
  assert(m_pConfig);
  assert(m_pSoundDevice);
  

  n_mMCUcycles = m_pConfig->GetMCUcycles ();
  LOGNOTE("MCU cycles %d", n_mMCUcycles);
  //LOGNOTE("Temp: %d", m_CPUThrottle.GetTemperature ());

  if (!m_UI.Initialize ())
	{
    LOGERR("Failed to initialize UI");
		return false;
	}

	assert (m_pConfig);
	if (!m_Serial.Initialize(m_pConfig->GetMIDIBaudRate ())) 
    {
        LOGERR("Failed to initialize Serial MIDI");
        return false;
    }
	unsigned ser_options = m_Serial.GetOptions();
	// Ensure CR->CRLF translation is disabled for MIDI links
	ser_options &= ~(SERIAL_OPTION_ONLCR);
	m_Serial.SetOptions(ser_options);
   LOGNOTE("Serial MIDI Initialized");
   InitBankMappings();
    midiParser.Init(this);
    memset(mcu.lcd.LCD_Data, 0x20, sizeof(mcu.lcd.LCD_Data));
    mcu.mcu.pc=0; //mcu not running   

    if (!LoadMainRoms(m_pConfig->GetExpRom())) {
        return false;
    }
    
    int ret = 0;

    uint8_t* nvram = (uint8_t*)m_romInfos[0].data;  // jv880_nvram.bin
    uint8_t* rom1 = (uint8_t*)m_romInfos[1].data;   // jv880_rom1.bin
    uint8_t* rom2 = (uint8_t*)m_romInfos[2].data;   // jv880_rom2.bin
    uint8_t* pcm1 = (uint8_t*)m_romInfos[3].data;   // jv880_waverom1.bin
    uint8_t* pcm2 = (uint8_t*)m_romInfos[4].data;   // jv880_waverom2.bin
    if (m_pConfig->GetExpRom() == 0) {
        ret = mcu.startSC55(rom1, rom2, pcm1, pcm2, nvram, nullptr); 
    } else {
        uint8_t* exp1 = (uint8_t*)m_romInfos[m_pConfig->GetExpRom() + 6].data;
        ret = mcu.startSC55(rom1, rom2, pcm1, pcm2, nvram, exp1);
    }
    if (!ret) {
        LOGNOTE("SC55 emulator started");
    }
    
  // setup and start the sound device
  int Channels = 2; // 16-bit Stereo
  // Need 2 x ChunkSize / Channel queue frames as the audio driver uses
  // two DMA channels each of ChunkSize and one single single frame
  // contains a sample for each of all the channels.
  //
  // See discussion here: https://github.com/rsta2/circle/discussions/453
  if (!m_pSoundDevice->AllocateQueueFrames(2 * m_pConfig->GetChunkSize() /
                                           Channels)) {
    LOGERR("Cannot allocate sound queue");

    return false;
  }

  m_pSoundDevice->SetWriteFormat(SoundFormatSigned16, Channels);

  m_nQueueSizeFrames = m_pSoundDevice->GetQueueSizeFrames();

  m_pSoundDevice->Start();

  if (!CMultiCoreSupport::Initialize ())
	{
		return false;
	}

  InitNetwork();  // returns bool but we continue even if something goes wrong
  LOGNOTE("CMiniJV880::Initialize: InitNetwork() called");

  LOGNOTE("initialised");
  size_t freeMemory = CMemorySystem::Get()->GetHeapFreeSpace(HEAP_ANY);
  LOGNOTE("Free memory: %u bytes (%.2f MB)", freeMemory, (float)freeMemory / (1024.0f * 1024.0f));
  return true;
}

void CMiniJV880::Process(bool bPlugAndPlayUpdated) {
    
    CScheduler* const pScheduler = CScheduler::Get();

    ProcessMIDISurface();
    pScheduler->Yield();

    m_UI.Process ();
    pScheduler->Yield();
    
    if (m_pNet) {
		UpdateNetwork();
	}
    pScheduler->Yield();

        uint8_t pendingBank = m_nPendingBankSwitch.load(std::memory_order_acquire);
    if (pendingBank != 0xFF) { // 0xFF = нет ожидающего переключения
        unsigned timestamp = m_nBankSwitchTimestamp.load(std::memory_order_acquire);
        unsigned elapsed = CTimer::GetClockTicks() - timestamp;

        //LOGNOTE("Pending bank %d, elapsed: %u us", pendingBank, elapsed);
        
        if (elapsed >= BANK_SWITCH_DEBOUNCE_US) {
            switchPatchBank(pendingBank);
            pScheduler->Yield();
            m_nPendingBankSwitch.store(0xFF, std::memory_order_release);
        }
    }

    int nRead = m_Serial.Read(m_MIDIBuffer, sizeof(m_MIDIBuffer));
    pScheduler->Yield();

    if (nRead > 0) {
        midiParser.FeedSerialBytes(m_MIDIBuffer, nRead);  
        pScheduler->Yield();
    }

  if (!bPlugAndPlayUpdated)
    return;

  if (m_pMIDIDevice == 0) {
    m_pMIDIDevice =
        (CUSBMIDIDevice *)CDeviceNameService::Get()->GetDevice("umidi1", FALSE);
    if (m_pMIDIDevice != 0) {
      m_pMIDIDevice->RegisterPacketHandler(USBMIDIMessageHandler);
      m_pMIDIDevice->RegisterRemovedHandler(DeviceRemovedHandler, this);
      memset(m_nMIDISurfaceLastLED, 0xFF, sizeof m_nMIDISurfaceLastLED);
      MarkMIDISurfaceLEDsDirty();
    }
  }
    
}

void CMiniJV880::USBMIDIMessageHandler(unsigned nCable, u8 *pPacket,
                                       unsigned nLength) {
  if (!pPacket || nLength == 0) return;
  s_pThis->midiParser.FeedUSBMIDIPacket(pPacket, nLength);
}


namespace
{
    static const uint8_t kPatchCommonBase[4] = {0x00, 0x08, 0x20, 0x00};
    static const uint8_t kPatchToneBaseThird[4] = {0x28, 0x29, 0x2A, 0x2B};

    static const uint8_t kToneParameterOffset[] = {
        0x03, // Tone Switch
        0x5C, // TVA Level
        0x5E, // TVA Pan (stored as two nibbles)
        0x71, // Reverb Send
        0x72, // Chorus Send
        0x69, // TVA ENV Time 1 (Attack)
        0x6B, // TVA ENV Time 2 (Decay)
        0x4B, // TVF Resonance
        0x4A  // TVF Cutoff
    };

    static const uint8_t kCommonParameterOffset[] = {
        0x18, // Patch Level
        0x19, // Patch Pan
        0x0E, // Reverb Level
        0x12  // Chorus Level
    };

    static void IncrementRolandAddress(uint8_t address[4])
    {
        for (int index = 3; index >= 0; --index)
        {
            address[index] = (address[index] + 1) & 0x7F;
            if (address[index] != 0) break;
        }
    }

    static uint8_t RolandChecksum(const uint8_t *data, unsigned length)
    {
        unsigned sum = 0;
        for (unsigned index = 0; index < length; ++index) sum += data[index] & 0x7F;
        return (uint8_t)((128 - (sum & 0x7F)) & 0x7F);
    }
}

bool CMiniJV880::IsPatchMode() const
{
    return (mcu.jv880_led_state & (1u << 5)) != 0;
}

unsigned CMiniJV880::GetMIDISurfaceBank() const
{
    return m_nMIDISurfaceBank.load(std::memory_order_acquire);
}

const char *CMiniJV880::GetMIDISurfaceRowLabel(unsigned row) const
{
    static const char *bank1[] = {"Vol", "Pan", "Rev", "Cho"};
    static const char *bank2[] = {"Atk", "Dcy", "Res", "Cut"};
    static const char *bank3[] = {"B3A", "B3B", "B3C", "B3D"};
    static const char *bank4[] = {"B4A", "B4B", "B4C", "B4D"};
    if (row >= 4) return "";
    switch (GetMIDISurfaceBank()) {
    case 2: return bank2[row];
    case 3: return bank3[row];
    case 4: return bank4[row];
    default: return bank1[row];
    }
}

void CMiniJV880::FormatMIDISurfaceToneValue(unsigned row, unsigned tone,
                                             char *text, unsigned textSize) const
{
    if (text == nullptr || textSize == 0) return;
    snprintf(text, textSize, "---");
    if (row >= 4 || tone >= 4 || !IsPatchMode()) return;

    SurfaceToneParameter parameter;
    if (GetMIDISurfaceBank() == 1) {
        static const SurfaceToneParameter map[] = {
            SurfaceToneLevel, SurfaceTonePan, SurfaceToneReverb, SurfaceToneChorus
        };
        parameter = map[row];
    } else if (GetMIDISurfaceBank() == 2) {
        static const SurfaceToneParameter map[] = {
            SurfaceToneAttack, SurfaceToneDecay, SurfaceToneResonance, SurfaceToneCutoff
        };
        parameter = map[row];
    } else {
        return;
    }

    const unsigned valid = m_SurfaceToneValidMask[tone].load(std::memory_order_acquire);
    if ((valid & (1u << parameter)) == 0) return;
    const unsigned value = m_SurfaceToneValues[tone][parameter].load(std::memory_order_acquire);
    snprintf(text, textSize, "%3u", value > 127 ? 127 : value);
}

bool CMiniJV880::MIDISurfaceChannelMatches(uint8_t channel) const
{
    if (!m_bMIDISurfaceEnabled || m_nMIDISurfaceChannel == 0) return false;
    if (m_nMIDISurfaceChannel == 17) return true;
    return (m_nMIDISurfaceChannel - 1) == channel;
}

void CMiniJV880::ResetMIDISurfacePickup(bool toneControlsOnly)
{
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 8; ++column) {
            if (toneControlsOnly && column >= 4) continue;
            SurfacePickupState &state = m_MIDISurfacePickup[row * 8 + column];
            state.latched = !m_bMIDISurfacePickupEnabled;
            state.hasPrevious = false;
            state.previous = 0;
        }
    }
}

bool CMiniJV880::ApplyMIDISurfacePickup(unsigned control, unsigned target, uint8_t value)
{
    if (control >= 32) return false;
    SurfacePickupState &state = m_MIDISurfacePickup[control];
    if (!m_bMIDISurfacePickupEnabled || state.latched) return true;

    const unsigned low = target > m_nMIDISurfacePickupRange
        ? target - m_nMIDISurfacePickupRange : 0;
    const unsigned high = std::min(127u, target + m_nMIDISurfacePickupRange);
    bool crossed = value >= low && value <= high;
    if (!crossed && state.hasPrevious) {
        crossed = (state.previous < target && value > target)
               || (state.previous > target && value < target);
    }
    state.previous = value;
    state.hasPrevious = true;
    if (crossed) state.latched = true;
    return state.latched;
}

bool CMiniJV880::GetMIDISurfaceTarget(unsigned row, unsigned column,
                                      unsigned &target) const
{
    if (row >= 4 || column >= 8) return false;

    if (!IsPatchMode()) {
        if (row > 1) return false;
        const unsigned mask = m_SurfacePartFieldMask[column].load(std::memory_order_acquire);
        const unsigned bit = row == 0 ? 2u : 4u;
        if ((mask & bit) == 0) return false;
        target = row == 0
            ? m_SurfacePartLevel[column].load(std::memory_order_acquire)
            : m_SurfacePartPan[column].load(std::memory_order_acquire);
        return true;
    }

    if (column < 4) {
        SurfaceToneParameter parameter;
        if (GetMIDISurfaceBank() == 1) {
            static const SurfaceToneParameter map[] = {
                SurfaceToneLevel, SurfaceTonePan, SurfaceToneReverb, SurfaceToneChorus
            };
            parameter = map[row];
        } else if (GetMIDISurfaceBank() == 2) {
            static const SurfaceToneParameter map[] = {
                SurfaceToneAttack, SurfaceToneDecay, SurfaceToneResonance, SurfaceToneCutoff
            };
            parameter = map[row];
        } else return false;
        const unsigned valid = m_SurfaceToneValidMask[column].load(std::memory_order_acquire);
        if ((valid & (1u << parameter)) == 0) return false;
        target = m_SurfaceToneValues[column][parameter].load(std::memory_order_acquire);
        return true;
    }

    if (column == 4) {
        static const SurfaceCommonParameter map[] = {
            SurfaceCommonLevel, SurfaceCommonPan,
            SurfaceCommonReverb, SurfaceCommonChorus
        };
        const SurfaceCommonParameter parameter = map[row];
        const unsigned valid = m_SurfaceCommonValidMask.load(std::memory_order_acquire);
        if ((valid & (1u << parameter)) == 0) return false;
        target = m_SurfaceCommonValues[parameter].load(std::memory_order_acquire);
        return true;
    }

    if (column == 5) {
        static const SurfaceToneParameter map[] = {
            SurfaceToneAttack, SurfaceToneDecay, SurfaceToneResonance, SurfaceToneCutoff
        };
        const SurfaceToneParameter parameter = map[row];
        unsigned sum = 0, count = 0;
        for (unsigned tone = 0; tone < 4; ++tone) {
            const unsigned valid = m_SurfaceToneValidMask[tone].load(std::memory_order_acquire);
            if ((valid & (1u << SurfaceToneSwitch)) == 0
                || (valid & (1u << parameter)) == 0
                || m_SurfaceToneValues[tone][SurfaceToneSwitch].load(std::memory_order_acquire) == 0)
                continue;
            sum += m_SurfaceToneValues[tone][parameter].load(std::memory_order_acquire);
            ++count;
        }
        if (count == 0) return false;
        target = (sum + count / 2) / count;
        return true;
    }
    return false;
}

void CMiniJV880::SendRolandRQ1(const uint8_t address[4], const uint8_t size[4])
{
    if (!address || !size) return;
    uint8_t message[15] = {0xF0, 0x41, (uint8_t)m_nMIDISurfaceDeviceID,
                           0x46, 0x11, 0,0,0,0, 0,0,0,0, 0, 0xF7};
    memcpy(&message[5], address, 4);
    memcpy(&message[9], size, 4);
    message[13] = RolandChecksum(&message[5], 8);
    mcu.postMidiSC55(message, sizeof message);
}

void CMiniJV880::SendRolandDT1(const uint8_t address[4],
                               const uint8_t *data, unsigned length)
{
    if (!address || !data || length == 0 || length > 240) return;
    uint8_t message[256];
    unsigned pos = 0;
    message[pos++] = 0xF0;
    message[pos++] = 0x41;
    message[pos++] = (uint8_t)m_nMIDISurfaceDeviceID;
    message[pos++] = 0x46;
    message[pos++] = 0x12;
    memcpy(&message[pos], address, 4); pos += 4;
    memcpy(&message[pos], data, length); pos += length;
    message[pos++] = RolandChecksum(&message[5], 4 + length);
    message[pos++] = 0xF7;
    mcu.postMidiSC55(message, pos);
}

void CMiniJV880::SetToneParameter(unsigned tone,
                                  SurfaceToneParameter parameter, uint8_t value)
{
    if (tone >= 4 || parameter >= SurfaceToneParameterCount) return;
    uint8_t address[4] = {0x00, 0x08, kPatchToneBaseThird[tone],
                          kToneParameterOffset[parameter]};
    value &= 0x7F;
    if (parameter == SurfaceTonePan) {
        const uint8_t pan[2] = {
            (uint8_t)((value >> 4) & 0x0F), (uint8_t)(value & 0x0F)
        };
        SendRolandDT1(address, pan, 2);
    } else {
        SendRolandDT1(address, &value, 1);
    }
    m_SurfaceToneValues[tone][parameter].store(value, std::memory_order_release);
    m_SurfaceToneValidMask[tone].fetch_or(1u << parameter, std::memory_order_acq_rel);
    m_nSurfaceLastPoll = 0;
    MarkMIDISurfaceLEDsDirty();
}

void CMiniJV880::SetCommonParameter(SurfaceCommonParameter parameter, uint8_t value)
{
    if (parameter >= SurfaceCommonParameterCount) return;
    uint8_t address[4] = {0x00, 0x08, 0x20, kCommonParameterOffset[parameter]};
    value &= 0x7F;
    SendRolandDT1(address, &value, 1);
    m_SurfaceCommonValues[parameter].store(value, std::memory_order_release);
    m_SurfaceCommonValidMask.fetch_or(1u << parameter, std::memory_order_acq_rel);
    m_nSurfaceLastPoll = 0;
}

void CMiniJV880::SetGlobalToneMacro(SurfaceToneParameter parameter, uint8_t value)
{
    bool sent = false;
    for (unsigned tone = 0; tone < 4; ++tone) {
        const unsigned valid = m_SurfaceToneValidMask[tone].load(std::memory_order_acquire);
        if ((valid & (1u << SurfaceToneSwitch)) != 0
            && m_SurfaceToneValues[tone][SurfaceToneSwitch].load(std::memory_order_acquire) != 0) {
            SetToneParameter(tone, parameter, value);
            sent = true;
        }
    }
    if (!sent) {
        for (unsigned tone = 0; tone < 4; ++tone)
            SetToneParameter(tone, parameter, value);
    }
}

void CMiniJV880::SetPerformanceParameter(unsigned part, unsigned offset, uint8_t value)
{
    if (part >= 8) return;
    uint8_t address[4] = {0x00, 0x00, (uint8_t)(0x18 + part), (uint8_t)offset};
    value &= 0x7F;
    SendRolandDT1(address, &value, 1);
    if (offset == 0x15) {
        m_SurfacePartEnabled[part].store(value != 0, std::memory_order_release);
        m_SurfacePartFieldMask[part].fetch_or(1u, std::memory_order_acq_rel);
        MarkMIDISurfaceLEDsDirty();
    } else if (offset == 0x19) {
        m_SurfacePartLevel[part].store(value, std::memory_order_release);
        m_SurfacePartFieldMask[part].fetch_or(2u, std::memory_order_acq_rel);
    } else if (offset == 0x1A) {
        m_SurfacePartPan[part].store(value, std::memory_order_release);
        m_SurfacePartFieldMask[part].fetch_or(4u, std::memory_order_acq_rel);
    }
    m_nSurfaceLastPoll = 0;
}

bool CMiniJV880::HandleMIDISurfaceCC(uint8_t channel, uint8_t cc, uint8_t value)
{
    if (!MIDISurfaceChannelMatches(channel)) return false;
    unsigned row = 4, column = 0;
    for (unsigned candidate = 0; candidate < 4; ++candidate) {
        const unsigned start = m_nMIDISurfaceCCStart[candidate];
        if (cc >= start && cc < start + 8) {
            row = candidate;
            column = cc - start;
            break;
        }
    }
    if (row >= 4) return false;

    unsigned target = 0;
    if (!GetMIDISurfaceTarget(row, column, target)) {
        // Dedicated surface CCs are consumed even while their cache is loading
        // or when the selected bank/row is intentionally reserved.
        return true;
    }
    if (!ApplyMIDISurfacePickup(row * 8 + column, target, value)) return true;

    if (!IsPatchMode()) {
        if (row == 0) SetPerformanceParameter(column, 0x19, value);
        else if (row == 1) SetPerformanceParameter(column, 0x1A, value);
        return true;
    }

    if (column < 4) {
        if (GetMIDISurfaceBank() == 1) {
            static const SurfaceToneParameter map[] = {
                SurfaceToneLevel, SurfaceTonePan, SurfaceToneReverb, SurfaceToneChorus
            };
            SetToneParameter(column, map[row], value);
        } else if (GetMIDISurfaceBank() == 2) {
            static const SurfaceToneParameter map[] = {
                SurfaceToneAttack, SurfaceToneDecay, SurfaceToneResonance, SurfaceToneCutoff
            };
            SetToneParameter(column, map[row], value);
        }
    } else if (column == 4) {
        static const SurfaceCommonParameter map[] = {
            SurfaceCommonLevel, SurfaceCommonPan,
            SurfaceCommonReverb, SurfaceCommonChorus
        };
        SetCommonParameter(map[row], value);
    } else if (column == 5) {
        static const SurfaceToneParameter map[] = {
            SurfaceToneAttack, SurfaceToneDecay, SurfaceToneResonance, SurfaceToneCutoff
        };
        SetGlobalToneMacro(map[row], value);
    }
    return true;
}

void CMiniJV880::SelectSurfaceTone(unsigned tone)
{
    if (tone >= 4 || !IsPatchMode()) return;
    const unsigned steps = (tone + 4 - m_nMIDISurfaceSelectedTone) % 4;
    for (unsigned step = 0; step < steps; ++step) {
        m_UI.TriggerUIButtonEvent(CUIButton::BtnEventToneSelect);
        CTimer::SimpleMsDelay(15);
        m_UI.TriggerUIButtonEvent(CUIButton::BtnEventRelease);
        CTimer::SimpleMsDelay(15);
    }
    m_nMIDISurfaceSelectedTone = tone;
    MarkMIDISurfaceLEDsDirty();
}

void CMiniJV880::QueuePatchBank(int bankNumber)
{
    if (bankNumber < 0 || bankNumber > 99) return;
    m_nPendingBankSwitch.store(bankNumber, std::memory_order_release);
    m_nBankSwitchTimestamp.store(CTimer::GetClockTicks(), std::memory_order_release);
}

void CMiniJV880::SelectAdjacentExpansion(int direction)
{
    if (m_bankMappingsCount == 0) return;
    int candidates[32];
    unsigned count = 0;
    for (unsigned index = 0; index < m_bankMappingsCount && count < 32; ++index) {
        const int rom = m_bankMappings[index].romIndex;
        bool found = false;
        for (unsigned test = 0; test < count; ++test)
            if (candidates[test] == rom) found = true;
        if (!found) candidates[count++] = rom;
    }
    if (count == 0) return;
    std::sort(candidates, candidates + count);

    int nextRom;
    unsigned current = 0;
    while (current < count && candidates[current] != m_currentExpansionRomIndex) ++current;
    if (current >= count)
        nextRom = direction >= 0 ? candidates[0] : candidates[count - 1];
    else
        nextRom = candidates[direction >= 0
            ? (current + 1) % count
            : (current + count - 1) % count];

    int targetBank = 100;
    for (unsigned index = 0; index < m_bankMappingsCount; ++index)
        if (m_bankMappings[index].romIndex == nextRom
            && m_bankMappings[index].bankNumber < targetBank)
            targetBank = m_bankMappings[index].bankNumber;
    if (targetBank <= 99) QueuePatchBank(targetBank);
}

void CMiniJV880::SelectAdjacentBank(int direction)
{
    int candidates[100];
    unsigned count = 0;
    for (unsigned index = 0; index < m_bankMappingsCount && count < 100; ++index)
        if (m_currentExpansionRomIndex < 0
            || m_bankMappings[index].romIndex == m_currentExpansionRomIndex)
            candidates[count++] = m_bankMappings[index].bankNumber;
    if (count == 0) return;
    std::sort(candidates, candidates + count);

    int target;
    unsigned current = 0;
    while (current < count && candidates[current] != m_currentBankNumber) ++current;
    if (current >= count)
        target = direction >= 0 ? candidates[0] : candidates[count - 1];
    else
        target = candidates[direction >= 0
            ? (current + 1) % count
            : (current + count - 1) % count];
    QueuePatchBank(target);
}

bool CMiniJV880::HandleMIDISurfaceButton(uint8_t channel, uint8_t number, bool pressed)
{
    if (!MIDISurfaceChannelMatches(channel)) return false;
    int button = -1;
    for (unsigned index = 0; index < 16; ++index)
        if (m_nMIDISurfaceButtons[index] != 0
            && number == m_nMIDISurfaceButtons[index]) {
            button = (int)index;
            break;
        }
    if (button < 0) return false;
    if (!pressed) return true;

    if (button < 8) {
        if (IsPatchMode()) {
            if (button < 4) {
                const unsigned valid = m_SurfaceToneValidMask[button].load(std::memory_order_acquire);
                if (valid & (1u << SurfaceToneSwitch)) {
                    const uint8_t next = m_SurfaceToneValues[button][SurfaceToneSwitch]
                        .load(std::memory_order_acquire) ? 0 : 1;
                    SetToneParameter(button, SurfaceToneSwitch, next);
                } else {
                    m_nSurfaceLastPoll = 0;
                }
            } else {
                SelectSurfaceTone(button - 4);
            }
        } else {
            const unsigned valid = m_SurfacePartFieldMask[button].load(std::memory_order_acquire);
            if (valid & 1u) {
                const uint8_t next = m_SurfacePartEnabled[button]
                    .load(std::memory_order_acquire) ? 0 : 1;
                SetPerformanceParameter(button, 0x15, next);
            } else {
                m_nSurfaceLastPoll = 0;
            }
        }
        return true;
    }

    if (button < 12) {
        m_nMIDISurfaceBank.store((unsigned)(button - 7), std::memory_order_release);
        ResetMIDISurfacePickup(true);
        MarkMIDISurfaceLEDsDirty();
        return true;
    }

    if (button == 12) SelectAdjacentExpansion(-1);
    else if (button == 13) SelectAdjacentExpansion(+1);
    else if (button == 14) SelectAdjacentBank(-1);
    else if (button == 15) SelectAdjacentBank(+1);
    return true;
}

void CMiniJV880::DrainEmulatedMIDIOut()
{
    uint8_t byte;
    while (mcu.ReadUARTTX(&byte)) {
        if (byte == 0xF0) {
            m_bSurfaceTXInSysEx = true;
            m_nSurfaceTXSysExLength = 0;
        }
        if (!m_bSurfaceTXInSysEx) continue;
        if (m_nSurfaceTXSysExLength < sizeof m_SurfaceTXSysEx)
            m_SurfaceTXSysEx[m_nSurfaceTXSysExLength++] = byte;
        else {
            m_bSurfaceTXInSysEx = false;
            m_nSurfaceTXSysExLength = 0;
            continue;
        }
        if (byte == 0xF7) {
            ParseEmulatedSysEx(m_SurfaceTXSysEx, m_nSurfaceTXSysExLength);
            m_bSurfaceTXInSysEx = false;
            m_nSurfaceTXSysExLength = 0;
        }
    }
}

void CMiniJV880::ParseEmulatedSysEx(const uint8_t *message, unsigned length)
{
    if (!message || length < 12 || message[0] != 0xF0 || message[length - 1] != 0xF7)
        return;
    if (message[1] != 0x41 || message[3] != 0x46 || message[4] != 0x12) return;

    const unsigned dataLength = length - 11;
    const uint8_t *address = &message[5];
    const uint8_t *data = &message[9];
    unsigned sum = message[length - 2] & 0x7F;
    for (unsigned index = 0; index < 4; ++index) sum += address[index] & 0x7F;
    for (unsigned index = 0; index < dataLength; ++index) sum += data[index] & 0x7F;
    if ((sum & 0x7F) != 0) return;
    UpdateSurfaceCacheFromDT1(address, data, dataLength);
}

void CMiniJV880::UpdateSurfaceCacheFromDT1(const uint8_t startAddress[4],
                                            const uint8_t *data, unsigned length)
{
    if (!startAddress || !data) return;
    uint8_t address[4] = {
        (uint8_t)(startAddress[0] & 0x7F), (uint8_t)(startAddress[1] & 0x7F),
        (uint8_t)(startAddress[2] & 0x7F), (uint8_t)(startAddress[3] & 0x7F)
    };
    bool patchNameChanged = false;

    for (unsigned index = 0; index < length; ++index, IncrementRolandAddress(address)) {
        const uint8_t value = data[index] & 0x7F;
        if (address[0] == 0x00 && address[1] == 0x08 && address[2] == 0x20) {
            const unsigned offset = address[3];
            if (offset < 12) {
                if (m_bSurfacePatchNameValid && m_SurfacePatchName[offset] != value)
                    patchNameChanged = true;
                m_SurfacePatchName[offset] = value;
                if (offset == 11) m_bSurfacePatchNameValid = true;
            }
            for (unsigned parameter = 0; parameter < SurfaceCommonParameterCount; ++parameter)
                if (offset == kCommonParameterOffset[parameter]) {
                    m_SurfaceCommonValues[parameter].store(value, std::memory_order_release);
                    m_SurfaceCommonValidMask.fetch_or(1u << parameter, std::memory_order_acq_rel);
                }
        }

        if (address[0] == 0x00 && address[1] == 0x08
            && address[2] >= 0x28 && address[2] <= 0x2B) {
            const unsigned tone = address[2] - 0x28;
            const unsigned offset = address[3];
            if (offset == kToneParameterOffset[SurfaceTonePan]) {
                unsigned pan = (value & 0x0F) << 4;
                if (index + 1 < length) pan |= data[index + 1] & 0x0F;
                if (pan > 127) pan = 127;
                m_SurfaceToneValues[tone][SurfaceTonePan].store(pan, std::memory_order_release);
                m_SurfaceToneValidMask[tone].fetch_or(1u << SurfaceTonePan, std::memory_order_acq_rel);
            }
            for (unsigned parameter = 0; parameter < SurfaceToneParameterCount; ++parameter) {
                if (parameter == SurfaceTonePan) continue;
                if (offset == kToneParameterOffset[parameter]) {
                    const unsigned old = m_SurfaceToneValues[tone][parameter]
                        .load(std::memory_order_acquire);
                    m_SurfaceToneValues[tone][parameter].store(value, std::memory_order_release);
                    m_SurfaceToneValidMask[tone].fetch_or(1u << parameter, std::memory_order_acq_rel);
                    if (parameter == SurfaceToneSwitch && old != value)
                        MarkMIDISurfaceLEDsDirty();
                }
            }
        }

        if (address[0] == 0x00 && address[1] == 0x00
            && address[2] >= 0x18 && address[2] <= 0x1F) {
            const unsigned part = address[2] - 0x18;
            if (address[3] == 0x15) {
                const unsigned old = m_SurfacePartEnabled[part].load(std::memory_order_acquire);
                m_SurfacePartEnabled[part].store(value != 0, std::memory_order_release);
                m_SurfacePartFieldMask[part].fetch_or(1u, std::memory_order_acq_rel);
                if (old != (value != 0)) MarkMIDISurfaceLEDsDirty();
            } else if (address[3] == 0x19) {
                m_SurfacePartLevel[part].store(value, std::memory_order_release);
                m_SurfacePartFieldMask[part].fetch_or(2u, std::memory_order_acq_rel);
            } else if (address[3] == 0x1A) {
                m_SurfacePartPan[part].store(value, std::memory_order_release);
                m_SurfacePartFieldMask[part].fetch_or(4u, std::memory_order_acq_rel);
            }
        }
    }

    if (patchNameChanged) {
        ResetMIDISurfacePickup();
        MarkMIDISurfaceLEDsDirty();
        m_nSurfacePollPhase = 1;
        m_nSurfaceLastPoll = 0;
    }
}

void CMiniJV880::PollMIDISurface()
{
    if (!m_bMIDISurfaceEnabled) return;
    const unsigned now = CTimer::GetClockTicks();
    if (m_nSurfaceLastPoll != 0 && now - m_nSurfaceLastPoll < 100000) return;
    m_nSurfaceLastPoll = now;

    if (IsPatchMode()) {
        const unsigned phase = m_nSurfacePollPhase++ % 5;
        if (phase == 0) {
            const uint8_t size[4] = {0x00, 0x00, 0x00, 0x1A};
            SendRolandRQ1(kPatchCommonBase, size);
        } else {
            const uint8_t address[4] = {
                0x00, 0x08, kPatchToneBaseThird[phase - 1], 0x03
            };
            const uint8_t size[4] = {0x00, 0x00, 0x00, 0x70};
            SendRolandRQ1(address, size);
        }
    } else {
        const unsigned part = m_nSurfacePollPhase++ % 8;
        const uint8_t address[4] = {0x00, 0x00, (uint8_t)(0x18 + part), 0x15};
        const uint8_t size[4] = {0x00, 0x00, 0x00, 0x06};
        SendRolandRQ1(address, size);
    }
}

void CMiniJV880::InvalidateMIDISurfaceState()
{
    m_SurfaceCommonValidMask.store(0, std::memory_order_release);
    for (unsigned tone = 0; tone < 4; ++tone)
        m_SurfaceToneValidMask[tone].store(0, std::memory_order_release);
    for (unsigned part = 0; part < 8; ++part)
        m_SurfacePartFieldMask[part].store(0, std::memory_order_release);
    m_bSurfacePatchNameValid = false;
    ResetMIDISurfacePickup();
    m_nSurfacePollPhase = 0;
    m_nSurfaceLastPoll = 0;
    MarkMIDISurfaceLEDsDirty();
}

void CMiniJV880::MarkMIDISurfaceLEDsDirty()
{
    m_bMIDISurfaceLEDDirty = true;
}

bool CMiniJV880::SendLaunchControlLED(unsigned button, uint8_t colour)
{
    if (button >= 16 || !m_pMIDIDevice) return false;
    const uint8_t index = button < 8
        ? (uint8_t)(0x18 + button)
        : (uint8_t)(0x20 + button - 8);
    const uint8_t message[] = {
        0xF0, 0x00, 0x20, 0x29, 0x02, 0x11, 0x78,
        (uint8_t)m_nMIDISurfaceTemplate, index, colour, 0xF7
    };
    CUSBMIDIDevice *device = m_pMIDIDevice;
    return device != nullptr
        && device->SendPlainMIDI(0, message, sizeof message, 0);
}

void CMiniJV880::UpdateMIDISurfaceLEDs()
{
    if (!m_bMIDISurfaceEnabled || !m_bMIDISurfaceLEDFeedback || !m_pMIDIDevice)
        return;
    const unsigned now = CTimer::GetClockTicks();
    if (!m_bMIDISurfaceLEDDirty
        && m_nMIDISurfaceLastLEDUpdate != 0
        && now - m_nMIDISurfaceLastLEDUpdate < 1000000)
        return;
    if (m_nMIDISurfaceLastLEDUpdate != 0
        && now - m_nMIDISurfaceLastLEDUpdate < 20000)
        return;

    static const uint8_t OFF = 0x0C;
    static const uint8_t GREEN = 0x3C;
    static const uint8_t AMBER = 0x3F;
    uint8_t desired[16];
    memset(desired, OFF, sizeof desired);

    if (IsPatchMode()) {
        for (unsigned tone = 0; tone < 4; ++tone) {
            const unsigned valid = m_SurfaceToneValidMask[tone].load(std::memory_order_acquire);
            if ((valid & (1u << SurfaceToneSwitch)) != 0
                && m_SurfaceToneValues[tone][SurfaceToneSwitch]
                    .load(std::memory_order_acquire) != 0)
                desired[tone] = GREEN;
        }
        desired[4 + (m_nMIDISurfaceSelectedTone & 3u)] = AMBER;
    } else {
        for (unsigned part = 0; part < 8; ++part) {
            const unsigned valid = m_SurfacePartFieldMask[part].load(std::memory_order_acquire);
            if ((valid & 1u) != 0
                && m_SurfacePartEnabled[part].load(std::memory_order_acquire) != 0)
                desired[part] = GREEN;
        }
    }

    const unsigned bank = GetMIDISurfaceBank();
    if (bank >= 1 && bank <= 4) desired[7 + bank] = AMBER;

    bool allSent = true;
    for (unsigned button = 0; button < 16; ++button) {
        if (m_nMIDISurfaceLastLED[button] == desired[button]) continue;
        if (SendLaunchControlLED(button, desired[button]))
            m_nMIDISurfaceLastLED[button] = desired[button];
        else
            allSent = false;
    }
    m_nMIDISurfaceLastLEDUpdate = now;
    m_bMIDISurfaceLEDDirty = !allSent;
}

void CMiniJV880::ProcessMIDISurface()
{
    if (!m_bMIDISurfaceEnabled) return;
    DrainEmulatedMIDIOut();

    const bool patchMode = IsPatchMode();
    if (patchMode != m_bMIDISurfaceLastPatchMode) {
        m_bMIDISurfaceLastPatchMode = patchMode;
        InvalidateMIDISurfaceState();
    }

    if (patchMode) {
        const unsigned toneLEDs = (mcu.jv880_led_state >> 6) & 0x0F;
        for (unsigned tone = 0; tone < 4; ++tone)
            if (toneLEDs & (1u << tone)) {
                if (m_nMIDISurfaceSelectedTone != tone) {
                    m_nMIDISurfaceSelectedTone = tone;
                    MarkMIDISurfaceLEDsDirty();
                }
                break;
            }
    }

    PollMIDISurface();
    UpdateMIDISurfaceLEDs();
}

void CMiniJV880::HandleFullMIDIMessage(const uint8_t* pData, uint8_t nLength)
{
    if (nLength == 0) return;

    if (0) { // Log
        char buf[256];
        int len = 0;
        for (int i = 0; i < nLength && len < 250; i++) {
            len += snprintf(buf + len, sizeof(buf) - len, "%02X ", pData[i]);
        }
        if (len) buf[len-1] = 0;
        LOGNOTE(buf);
    }   

    uint8_t status = pData[0];

    auto MIDIButtonChannelMatches = [this](uint8_t channel) {
        if (m_UI.m_nMIDIButtonChannel == 0) return false;
        if (m_UI.m_nMIDIButtonChannel == 17) return true;
        return (m_UI.m_nMIDIButtonChannel - 1) == channel;
    };

    // ===== Priority 1: surface buttons by MIDI Note On/Off =====
    // The surface has its own channel and therefore is checked independently
    // from the legacy front-panel MIDIButtonCh mapping.
    if (m_UI.m_bMIDIButtonsUseNotes
        && ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
        && nLength == 3)
    {
        const uint8_t note = pData[1] & 0x7F;
        const bool pressed = (status & 0xF0) == 0x90 && pData[2] != 0;
        if (HandleMIDISurfaceButton(status & 0x0F, note, pressed)) return;
    }

    // ===== Priority 2: front-panel buttons by MIDI Note On/Off =====
    // Note On with velocity > 0 presses the virtual button.
    // Note Off, or Note On with velocity 0, releases it.
    if (m_UI.m_bMIDIButtonsUseNotes
        && ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
        && nLength == 3
        && MIDIButtonChannelMatches(status & 0x0F))
    {
        const uint8_t note = pData[1] & 0x7F;
        const bool pressed = (status & 0xF0) == 0x90 && pData[2] != 0;

        auto handleButton = [this, note, pressed](uint8_t configuredNote,
                                                  CUIButton::BtnEvent event) {
            if (configuredNote != 0 && note == configuredNote) {
                m_UI.TriggerUIButtonEvent(pressed
                    ? event
                    : CUIButton::BtnEventRelease);
                return true;
            }
            return false;
        };

        if (handleButton(m_UI.m_nMIDIPreview,      CUIButton::BtnEventPreview)) return;
        if (handleButton(m_UI.m_nMIDILeft,         CUIButton::BtnEventLeft)) return;
        if (handleButton(m_UI.m_nMIDIRight,        CUIButton::BtnEventRight)) return;
        if (handleButton(m_UI.m_nMIDIData,         CUIButton::BtnEventData)) return;
        if (handleButton(m_UI.m_nMIDIToneSelect,   CUIButton::BtnEventToneSelect)) return;
        if (handleButton(m_UI.m_nMIDIPatchPerform, CUIButton::BtnEventPatchPerform)) return;
        if (handleButton(m_UI.m_nMIDIEdit,         CUIButton::BtnEventEdit)) return;
        if (handleButton(m_UI.m_nMIDISystem,       CUIButton::BtnEventSystem)) return;
        if (handleButton(m_UI.m_nMIDIRhythm,       CUIButton::BtnEventRhythm)) return;
        if (handleButton(m_UI.m_nMIDIUtility,      CUIButton::BtnEventUtility)) return;
        if (handleButton(m_UI.m_nMIDIMute,         CUIButton::BtnEventMute)) return;
        if (handleButton(m_UI.m_nMIDIMonitor,      CUIButton::BtnEventMonitor)) return;
        if (handleButton(m_UI.m_nMIDICompare,      CUIButton::BtnEventCompare)) return;
        if (handleButton(m_UI.m_nMIDIEnter,        CUIButton::BtnEventEnter)) return;

        if (m_UI.m_nMIDIUp != 0 && note == m_UI.m_nMIDIUp) {
            if (pressed) mcu.MCU_EncoderTrigger(1);
            return;
        }
        if (m_UI.m_nMIDIDown != 0 && note == m_UI.m_nMIDIDown) {
            if (pressed) mcu.MCU_EncoderTrigger(0);
            return;
        }
        if (m_UI.m_nMIDISaveNVRAM != 0 && note == m_UI.m_nMIDISaveNVRAM) {
            if (pressed) SaveNVRAMIncremental();
            return;
        }
    }

    // ===== Priority 3: Musical Note On/Off =====
    if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) {
        if (nLength == 3) {
            mcu.postMidiSC55(pData, nLength);
            return;
        }
    }

    // ===== Priority 4: Pitch Bend =====
    if ((status & 0xF0) == 0xE0) {
        if (nLength == 3) {
            mcu.postMidiSC55(pData, nLength);
            return;
        }
    }

    // ===== Priority 5: dedicated control surface CC rows =====
    // It must precede normal Bank Select because the middle row starts at CC32.
    if ((status & 0xF0) == 0xB0 && nLength == 3
        && HandleMIDISurfaceCC(status & 0x0F, pData[1] & 0x7F, pData[2] & 0x7F))
        return;

    // ===== Priority 6: Modulation (CC 1) =====
    if ((status & 0xF0) == 0xB0 && nLength == 3 && pData[1] == 1) {
        mcu.postMidiSC55(pData, nLength);
        return;
    }

    // ===== Priority 7: Bank Switch (CC 0 MSB and CC 32 LSB) =====
    if ((status & 0xF0) == 0xB0 && nLength == 3) {
        uint8_t channel = status & 0x0F;
        
        // CC 0 - Bank MSB
        if (pData[1] == 0) {
            uint8_t msb = pData[2] & 0x7F;
            m_nBankMSB[channel] = msb;
            
            if (msb != 0) {
                // MSB != 0 - pass to parser
                mcu.postMidiSC55(pData, nLength);
            }
            return;
        }
        
        // CC 32 - Bank LSB
        if (pData[1] == 32) {
            uint8_t msb = m_nBankMSB[channel];
            uint8_t lsb = pData[2] & 0x7F;
            
            if (msb != 0) {
                // MSB != 0 - pass to parser
                mcu.postMidiSC55(pData, nLength);
                return;
            }
            
            // MSB = 0
            if (lsb == 80 || lsb == 81) {
                // LSB = 80 or 81 - pass to parser 
                mcu.postMidiSC55(pData, nLength);
                return;
            }
            
            // MSB = 0, LSB = 0-79, 82-127 - switch patch bank
            QueuePatchBank(lsb);
            return;
        }
    }

    // ===== Priority 8: surface buttons in legacy CC mode =====
    if (!m_UI.m_bMIDIButtonsUseNotes
        && (status & 0xF0) == 0xB0 && nLength == 3
        && HandleMIDISurfaceButton(status & 0x0F, pData[1] & 0x7F,
                                   (pData[2] & 0x7F) < 64))
        return;

    // ===== Priority 9: UI Control Change messages =====
    // In Notes mode only the relative MIDI encoder remains on CC.
    // In legacy CC mode the buttons, Up/Down and NVRAM command also use CC.
    if ((status & 0xF0) == 0xB0 && nLength == 3
        && MIDIButtonChannelMatches(status & 0x0F))
    {
        const uint8_t ccNumber = pData[1] & 0x7F;
        const uint8_t ccValue = pData[2] & 0x7F;

        if (!m_UI.m_bMIDIButtonsUseNotes)
        {
            if (m_UI.m_nMIDISaveNVRAM != 0
                && ccNumber == m_UI.m_nMIDISaveNVRAM && ccValue < 64)
            {
                SaveNVRAMIncremental();
                return;
            }

            auto handleButton = [this, ccNumber, ccValue](uint8_t configuredCC,
                                                           CUIButton::BtnEvent event) {
                if (configuredCC != 0 && ccNumber == configuredCC) {
                    m_UI.TriggerUIButtonEvent(ccValue < 64
                        ? event
                        : CUIButton::BtnEventRelease);
                    return true;
                }
                return false;
            };

            if (handleButton(m_UI.m_nMIDIPreview,      CUIButton::BtnEventPreview)) return;
            if (handleButton(m_UI.m_nMIDILeft,         CUIButton::BtnEventLeft)) return;
            if (handleButton(m_UI.m_nMIDIRight,        CUIButton::BtnEventRight)) return;
            if (handleButton(m_UI.m_nMIDIData,         CUIButton::BtnEventData)) return;
            if (handleButton(m_UI.m_nMIDIToneSelect,   CUIButton::BtnEventToneSelect)) return;
            if (handleButton(m_UI.m_nMIDIPatchPerform, CUIButton::BtnEventPatchPerform)) return;
            if (handleButton(m_UI.m_nMIDIEdit,         CUIButton::BtnEventEdit)) return;
            if (handleButton(m_UI.m_nMIDISystem,       CUIButton::BtnEventSystem)) return;
            if (handleButton(m_UI.m_nMIDIRhythm,       CUIButton::BtnEventRhythm)) return;
            if (handleButton(m_UI.m_nMIDIUtility,      CUIButton::BtnEventUtility)) return;
            if (handleButton(m_UI.m_nMIDIMute,         CUIButton::BtnEventMute)) return;
            if (handleButton(m_UI.m_nMIDIMonitor,      CUIButton::BtnEventMonitor)) return;
            if (handleButton(m_UI.m_nMIDICompare,      CUIButton::BtnEventCompare)) return;
            if (handleButton(m_UI.m_nMIDIEnter,        CUIButton::BtnEventEnter)) return;

            if (m_UI.m_nMIDIUp != 0
                && ccNumber == m_UI.m_nMIDIUp && ccValue < 64)
            {
                m_UI.TriggerUIButtonEvent(CUIButton::BtnEventRelease);
                mcu.MCU_EncoderTrigger(1);
                return;
            }
            if (m_UI.m_nMIDIDown != 0
                && ccNumber == m_UI.m_nMIDIDown && ccValue < 64)
            {
                m_UI.TriggerUIButtonEvent(CUIButton::BtnEventRelease);
                mcu.MCU_EncoderTrigger(0);
                return;
            }
        }

        // The relative encoder remains a CC in both MIDIButtons modes.
        if (m_UI.m_nMIDIEncoder && ccNumber == m_UI.m_nMIDIEncoderCC)
        {
            if ((m_UI.m_nMIDIEncoderUp == 0 && ccValue < 64)
                || (m_UI.m_nMIDIEncoderUp != 0 && ccValue > 64))
            {
                m_UI.TriggerUIButtonEvent(CUIButton::BtnEventRelease);
                mcu.MCU_EncoderTrigger(1);
                return;
            }
            if ((m_UI.m_nMIDIEncoderDown == 0 && ccValue < 64)
                || (m_UI.m_nMIDIEncoderDown != 0 && ccValue > 64))
            {
                m_UI.TriggerUIButtonEvent(CUIButton::BtnEventRelease);
                mcu.MCU_EncoderTrigger(0);
                return;
            }
        }
    }
    // add checksum for Roland sysex messages 
    if (pData[0] == 0xF0 && nLength > 7 && pData[nLength-1] == 0xF7) {
        if (pData[1] == 0x41) { // Roland
            int chk_idx = nLength - 2;
            if (chk_idx < 6) return; 

            // Sum last 5 bytes before checksum
            int sum = 0;
            for (int i = 0; i < 5; i++) {
                sum += pData[chk_idx - 5 + i];
            }
            sum &= 0x7F;
            uint8_t checksum = (128 - sum) & 0x7F;

            uint8_t out[256];
            memcpy(out, pData, nLength);
            out[chk_idx] = checksum;

            

            mcu.postMidiSC55(out, nLength);
            return;
        }
    }

    
    mcu.postMidiSC55(pData, nLength);
}

void CMiniJV880::SaveNVRAMIncremental() {
    char filename[64];
    
    FRESULT dirRes = f_mkdir("nvram");
    if (dirRes != FR_OK && dirRes != FR_EXIST) {
        LOGERR("Cannot create nvram directory, error: %d", dirRes);
        return;
    }
    
    FIL file;
    FRESULT res;
    
    do {
        sprintf(filename, "nvram/jv880_nvram%d.bin", ++m_nNVRAMSaveCounter);
        
        res = f_open(&file, filename, FA_READ);
        if (res == FR_OK) {
            // File exists - close and try next number
            f_close(&file);
        }
    } while (res == FR_OK); 

    m_UI.LCDMessage("Saving NVRAM file\njv880_nvram%d.bin", m_nNVRAMSaveCounter);
    
    res = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        LOGERR("Cannot open file %s for writing, error: %d", filename, res);
        return;
    }
    
    UINT bytesWritten;
    res = f_write(&file, mcu.nvram, 0x8000, &bytesWritten);
    f_close(&file);
    
    if (res == FR_OK && bytesWritten == 0x8000) {
        LOGNOTE("NVRAM saved to %s", filename);
        m_UI.LCDMessage("Saved NVRAM file\njv880_nvram%d.bin", m_nNVRAMSaveCounter);
    } else {
        LOGERR("Failed to save NVRAM to %s, written: %d bytes, error: %d", 
               filename, bytesWritten, res);
    }
}


void CMiniJV880::DeviceRemovedHandler(CDevice *pDevice, void *pContext) {
  LOGERR("CMiniJV880::DeviceRemovedHandler");

  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(pContext);
  assert(pThis != 0);

  if (pDevice == pThis->m_pMIDIDevice) {
    pThis->m_pMIDIDevice = 0;
    memset(pThis->m_nMIDISurfaceLastLED, 0xFF, sizeof pThis->m_nMIDISurfaceLastLED);
    pThis->MarkMIDISurfaceLEDsDirty();
  }
}

void CMiniJV880::Run(unsigned nCore) {
    assert(1 <= nCore && nCore < CORES);
    //int nSamples = 0;
    

    if (nCore == 2) { // 2nd core - MCU + audio output
        const int MCU_INSTR_BURST = 64;
        while (true) {
            if (m_bAudioPaused.load(std::memory_order_acquire)) {
                CTimer::SimpleMsDelay(1);
                continue;
            }
            unsigned nFrames = m_nQueueSizeFrames - m_pSoundDevice->GetQueueFramesAvail();
            if (nFrames < m_nQueueSizeFrames / 2) {
                CTimer::SimpleMsDelay(1);
                continue;
            }
            int nSamples = (int)nFrames * 2;
            if (nSamples >= (int)AUDIO_BUFFER_SIZE) nSamples = (int)AUDIO_BUFFER_SIZE - 2;
            int16_t *out_buf = (int16_t*)malloc(nSamples * sizeof(int16_t));
            if (!out_buf) { CTimer::SimpleMsDelay(1); continue; }
            int out_pos = 0;
            while (out_pos < nSamples) {
                // 1) try to copy available audio first
                uint64_t w = __atomic_load_n(&sample_write_idx, __ATOMIC_ACQUIRE);
                uint64_t r = __atomic_load_n(&sample_read_idx,  __ATOMIC_RELAXED);
                uint64_t avail = w - r;
                if (avail > 0) {
                    uint32_t need = (uint32_t)(nSamples - out_pos);
                    uint32_t to_copy = (avail < need) ? (uint32_t)avail : need;
                    uint32_t idx = (uint32_t)(r & AUDIO_BUFFER_MASK);
                    uint32_t first = AUDIO_BUFFER_SIZE - idx;
                    if (first > to_copy) first = to_copy;
                    memcpy(&out_buf[out_pos], &sample_buffer[idx], first * sizeof(int16_t));
                    out_pos += first;
                    r += first;
                    uint32_t rem = to_copy - first;
                    if (rem) {
                        memcpy(&out_buf[out_pos], &sample_buffer[r & AUDIO_BUFFER_MASK], rem * sizeof(int16_t));
                        out_pos += rem;
                        r += rem;
                    }
                    __atomic_store_n(&sample_read_idx, r, __ATOMIC_RELEASE);
                    continue; // loop to fill remaining samples
                }
                // 2) if no samples ready, run bounded MCU burst to allow producer to progress
                int instr = 0;
                while (instr < MCU_INSTR_BURST) {
                    if (m_bAudioPaused.load(std::memory_order_acquire)) {
                        break; // Exit MCU burst immediately
                    }
                    if (!mcu.mcu.ex_ignore)
                        mcu.MCU_Interrupt_Handle();
                    else
                        mcu.mcu.ex_ignore = 0;

                    if (!mcu.mcu.sleep)
                        mcu.MCU_ReadInstruction();
                    mcu.mcu.cycles += n_mMCUcycles;
                    __atomic_store_n(&mcu.mcu.cycles, mcu.mcu.cycles, __ATOMIC_RELEASE);
                    mcu.TIMER_Clock(mcu.mcu.cycles);
                    mcu.MCU_UpdateUART_RX();
                    mcu.MCU_UpdateUART_TX();
                    mcu.MCU_UpdateAnalog(mcu.mcu.cycles);
                    ++instr;
                }
            } 

            // write to audio device
            int len = nSamples * sizeof(int16_t);
            if (m_pSoundDevice->Write(out_buf, len) != len) {
                LOGERR("Sound data dropped");
            }
            free(out_buf);
        }
    }
    else if (nCore == 3) { // 3rd core - PCM Update
        constexpr uint64_t MCU_CLOCK_HZ = 12000000ull; // if your MCU clock differs, set accordingly
        constexpr uint32_t AUDIO_RATE = 64000u;
        constexpr uint64_t CYCLES_PER_SAMPLE = MCU_CLOCK_HZ / AUDIO_RATE; // 375 typical for H8@12MHz
        const uint32_t MAX_SAMPLES_PER_ITER = 128; // bound to avoid huge bursts

        uint64_t last_generated_cycles = __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_RELAXED);

        while (true) {
            if (m_bAudioPaused.load(std::memory_order_acquire)) {
                last_generated_cycles = __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_ACQUIRE); // Синхронизация!
                CTimer::SimpleMsDelay(1);
                continue;
            }
            uint64_t cycles_target = __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_ACQUIRE);
            if (cycles_target <= last_generated_cycles) {
                CTimer::SimpleMsDelay(0);
                continue;
            }

            uint64_t cycles_avail = cycles_target - last_generated_cycles;
            uint64_t samples_to_gen = cycles_avail / CYCLES_PER_SAMPLE;
            while (samples_to_gen > 0) {
                uint32_t gen = (uint32_t) (samples_to_gen > MAX_SAMPLES_PER_ITER ? MAX_SAMPLES_PER_ITER : samples_to_gen);
                uint64_t pcm_target = last_generated_cycles + gen * CYCLES_PER_SAMPLE;

                mcu.pcm.PCM_Update(pcm_target);
                    
                last_generated_cycles = pcm_target;
                samples_to_gen -= gen;

                CTimer::SimpleMsDelay(0);
            }
        }
    }

}


bool CMiniJV880::LoadMainRoms(uint8_t ExpRom) {
    //LOGNOTE("Loading main ROMs for synthesizer and %d exp", ExpRom);
    
    int main_rom_indices[6];
    unsigned cr;
    
    if (ExpRom == 0 || ExpRom > 19) {
        const int indices[] = {0, 1, 2, 3, 4}; // nvram, rom1, rom2, waverom1, waverom2
        memcpy(main_rom_indices, indices, sizeof(indices));
        cr = 5;
    } else {
        const int indices[] = {0, 1, 2, 3, 4, ExpRom + 6}; // nvram, rom1, rom2, waverom1, waverom2, expansion
        memcpy(main_rom_indices, indices, sizeof(indices));
        cr = 6;
    }
    
    for (unsigned i = 0; i < cr; i++) {
        int rom_index = main_rom_indices[i];
        if (!LoadRom(rom_index)) {
            LOGERR("Failed to load ROM at index %d", rom_index);
            return false;
        }
    }
    
    LOGNOTE("All main ROMs loaded successfully");
    return true;
}

bool CMiniJV880::LoadRom(uint8_t rom_index) {
    
    if (rom_index >= ROM_COUNT) {
        LOGERR("Invalid ROM index: %d", rom_index);
        return false;
    }

    RomInfo& rom = m_romInfos[rom_index];
    std::string fullPath = "roms/";

    fullPath += rom.filename;

    // Check if file exists
    if (!FileExists(fullPath.c_str())) {
        LOGERR("ROM file not found: %s", rom.filename);
        return false;
    }
    m_UI.LCDMessage("Loading file\n%s", rom.filename);
    m_UI.RenderDisplay();
    if (m_UI.GetLCDBuffered()) m_UI.GetLCDBuffered()->Update(256);
    
    
    // Check if already loaded
    if (rom.isLoaded) {
        //LOGNOTE("ROM %s already loaded", fullPath.c_str());
        return true;
    }
    
    rom.data = malloc(rom.size);
    if (!rom.data) {
        LOGERR("Not enough memory for %s (size: %zu)", fullPath.c_str(), rom.size);
        return false;
    }
    
    if (!LoadFile(fullPath.c_str(), (uint8_t*)rom.data, rom.size)) {
        LOGERR("Cannot load %s", fullPath.c_str());
        free(rom.data);
        rom.data = nullptr;
        return false;
    }
    
    // Check if descrambling is needed
    if (rom.needsUnscramble) {
        uint8_t* descrambled_data = (uint8_t*)malloc(rom.size);
        if (!descrambled_data) {
            LOGERR("Not enough memory for descrambled %s", fullPath.c_str());
            free(rom.data);
            rom.data = nullptr;
            return false;
        }
        UnscrambleRom((uint8_t*)rom.data, descrambled_data, rom.size);
        free(rom.data);
        rom.data = descrambled_data;
    }
    
    LOGNOTE("Loaded file %s", rom.filename);

    CTimer::SimpleMsDelay(300);
    rom.isLoaded = true;
    return true;
}

bool CMiniJV880::FileExists(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

bool CMiniJV880::LoadFile(const char* filename, uint8_t* buffer, size_t size) {
    FIL f;
    unsigned int nBytesRead = 0;

    if (f_open(&f, filename, FA_READ | FA_OPEN_EXISTING) != FR_OK) {
        LOGERR("Cannot open %s", filename);
        return false;
    }
    FRESULT fr = f_read(&f, buffer, size, &nBytesRead);
    f_close(&f);

    if (fr != FR_OK) {
        LOGERR("f_read error %d for %s", fr, filename);
        return false;
    }

    if (nBytesRead != size) {
        LOGERR("Unexpected file size for %s: expected %zu, read %u", filename, size, nBytesRead);
        return false;
    }

    return true;
}

void CMiniJV880::UnscrambleRom(const uint8_t *src, uint8_t *dst, int len) {
    for (int i = 0; i < len; i++) {
        int address = i & ~0xfffff;
        static const int aa[] = {2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19};
        for (int j = 0; j < 20; j++) {
            if (i & (1 << j))
                address |= 1 << aa[j];
        }
        uint8_t srcdata = src[address];
        uint8_t data = 0;
        static const int dd[] = {2, 0, 4, 5, 7, 6, 3, 1};
        for (int j = 0; j < 8; j++) {
            if (srcdata & (1 << dd[j]))
                data |= 1 << j;
        }
        dst[i] = data;
    }
}

// Read all patchbank settings on start
void CMiniJV880::InitBankMappings() { 
    // Initialize array
    m_bankMappingsCount = 0;
    m_bankMappingsCapacity = 32;  // Initial capacity
    m_bankMappings = new BankMapping[m_bankMappingsCapacity];
    
    // Scan patch folder for files like XXnvramYY.bin
    DIR dir;
    FILINFO fno;
    
    FRESULT res = f_opendir(&dir, "patch");
    if (res != FR_OK) {
        LOGERR("Cannot open patch directory: %d", res);
        return;
    }
    
    while (true) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        // Check if filename contains "nvram" and ".bin"
        if (strstr(fno.fname, "nvram") && strstr(fno.fname, ".bin")) {
            ParseAndAddMapping(fno.fname);
        }
    }
    
    f_closedir(&dir);
    
    LOGNOTE("Loaded %u bank mappings", m_bankMappingsCount);
}

void CMiniJV880::ParseAndAddMapping(const char* filename) {
    // Format: XXnvramYY.bin
    // Minimum length check: "00nvram00.bin" = 13 chars
    if (strlen(filename) < 13) return;
    
    // Extract XX (first 2 characters must be digits)
    if (!isdigit(filename[0]) || !isdigit(filename[1])) return;
    int bankNumber = (filename[0] - '0') * 10 + (filename[1] - '0');
    
    // Extract YY (chars at position 7 and 8: "XXnvramYY")
    if (!isdigit(filename[7]) || !isdigit(filename[8])) return;
    int yyNumber = (filename[7] - '0') * 10 + (filename[8] - '0');
    int romIndex = yyNumber + 6;
    
    // Resize array if needed
    if (m_bankMappingsCount >= m_bankMappingsCapacity) {
        m_bankMappingsCapacity *= 2;
        BankMapping* newArray = new BankMapping[m_bankMappingsCapacity];
        memcpy(newArray, m_bankMappings, m_bankMappingsCount * sizeof(BankMapping));
        delete[] m_bankMappings;
        m_bankMappings = newArray;
    }
    
    m_bankMappings[m_bankMappingsCount].bankNumber = bankNumber;
    m_bankMappings[m_bankMappingsCount].romIndex = romIndex;
    strncpy(m_bankMappings[m_bankMappingsCount].nvramFilename, filename, sizeof(m_bankMappings[m_bankMappingsCount].nvramFilename) - 1);
    m_bankMappings[m_bankMappingsCount].nvramFilename[sizeof(m_bankMappings[m_bankMappingsCount].nvramFilename) - 1] = '\0';
    m_bankMappingsCount++;
    
    //LOGNOTE("Mapped bank %d -> ROM index %d, nvram: %s", bankNumber, romIndex, filename);
}

void CMiniJV880::switchPatchBank(int bankNumber) {
    if (bankNumber < 0 || bankNumber > 99) return;
    
    int romIndex = -1;
    const char* nvramFilename = nullptr;
    for (unsigned i = 0; i < m_bankMappingsCount; i++) {
        if (m_bankMappings[i].bankNumber == bankNumber) {
            romIndex = m_bankMappings[i].romIndex;
            nvramFilename = m_bankMappings[i].nvramFilename;
            break;
        }
    }
    
    if (romIndex == -1) {
        LOGNOTE("Bank %d not found in mapping", bankNumber);
        return;
    }
    
    if (romIndex < 0 || (size_t)romIndex >= ROM_COUNT) {
        LOGERR("ROM index %d out of range for bank %d", romIndex, bankNumber);
        return;
    }
    
    RomInfo& rom = m_romInfos[romIndex];
    
    // Check if ROM file exists
    if (rom.filename == nullptr) {
        LOGERR("ROM file not defined for index %d (bank %d)", romIndex, bankNumber);
        return;
    }
    
    // Special handling for bank 0: load NVRAM only, don't change expansion ROM
    bool loadRomData = (bankNumber != 0);
    
    // Try to load ROM if not loaded (skip for bank 0)
    if (loadRomData && !rom.isLoaded && !LoadRom(romIndex)) {
        LOGERR("Failed to load ROM %s for bank %d", rom.filename, bankNumber);
        return;
    }
    
    // 1. Stop EVERYTHING
    m_bAudioPaused.store(true, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst); // Ensure visible
    CTimer::SimpleMsDelay(300);  // Longer delay to ensure cores exit even from stuck MCU instructions

    mcu.mcu.ex_ignore = 1;  // Ignore interrupts
    mcu.ga_int_enable = 0;  // Disable interrupts
    mcu.ga_int_trigger = 0; // Clear triggers
    
    // 2. Copy ROM (skip for bank 0)
    if (loadRomData) {
        memcpy(mcu.pcm.waverom_exp, rom.data, EXP_SIZE);
    }
    
    // 3. Load NVRAM if mapping exists
    if (nvramFilename != nullptr) {
        char nvramPath[64];
        snprintf(nvramPath, sizeof(nvramPath), "patch/%s", nvramFilename);
        
        FIL file;
        FRESULT res = f_open(&file, nvramPath, FA_READ);
        if (res == FR_OK) {
            UINT bytesRead;
            res = f_read(&file, mcu.nvram, sizeof(mcu.nvram), &bytesRead);
            f_close(&file);
            
            if (res == FR_OK && bytesRead == sizeof(mcu.nvram)) {
                /*LOGNOTE("NVRAM loaded from %s (%u bytes), first 4 bytes: %02X %02X %02X %02X", 
                        nvramFilename, bytesRead,
                        mcu.nvram[0], mcu.nvram[1], mcu.nvram[2], mcu.nvram[3]);*/
            } else {
                LOGERR("Failed to read NVRAM from %s: res=%d, bytes=%u", nvramFilename, res, bytesRead);
            }
        } else {
            LOGERR("Failed to open NVRAM file %s: %d", nvramPath, res);
        }
    }
    
    // 4. Full reset (clears mcu.mcu.cycles!)
    mcu.SC55_Reset();
    CTimer::SimpleMsDelay(300);
    
    // 5. CRITICAL: Zero sample_write_idx AFTER reset
    __atomic_store_n(&sample_write_idx, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sample_read_idx, 0, __ATOMIC_RELEASE);
    
    std::atomic_thread_fence(std::memory_order_seq_cst);
    CTimer::SimpleMsDelay(50); // Small delay before resume
    
    // 6. Resume - Core 3 synchronizes automatically
    m_bAudioPaused.store(false, std::memory_order_release);
    CTimer::SimpleMsDelay(20);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    m_currentBankNumber = bankNumber;
    m_currentExpansionRomIndex = romIndex;
    InvalidateMIDISurfaceState();
    
    size_t freeAfter = CMemorySystem::Get()->GetHeapFreeSpace(HEAP_ANY);
    LOGNOTE("=== BANK SWITCHED TO: %d ROM index %d (%s), free mem=%.2f MB ===", bankNumber, 
            romIndex, rom.filename, (float)freeAfter/(1024.0f*1024.0f));
}

// additional temporary functions 
void CMiniJV880::LogPCM(uint64_t logcyc1) {
 return;
  LOGNOTE("PCM Update running | MCU cycles: %u", mcu.mcu.cycles);
 return; 
}

void CMiniJV880::LogMCU(uint64_t logcyc,uint64_t  logwriteptr,int  logsleep,int  logex) {
return;
  LOGNOTE("MCU cycles: %u | sample_write_ptr: %u | sleep: %d | ex_ignore: %d",
                                mcu.mcu.cycles, sample_write_idx,
                                mcu.mcu.sleep, mcu.mcu.ex_ignore);
 return;
}

// Network functions
void CMiniJV880::UpdateNetwork()
{

	if (!m_pNet) {
		LOGNOTE("CMiniJV880::UpdateNetwork: m_pNet is nullptr, returning early");
		return;
	}

    
	bool bNetIsRunning = m_pNet->IsRunning();
    if (m_pNetDevice->GetType() == NetDeviceTypeEthernet)
		bNetIsRunning &= m_pNetDevice->IsLinkUp();
	else if (m_pNetDevice->GetType() == NetDeviceTypeWLAN) {
		bNetIsRunning &= (m_WPASupplicant && m_WPASupplicant->IsConnected());
    }

    
	if (!m_bNetworkInit && bNetIsRunning)
	{
		LOGNOTE("CMiniJV880::UpdateNetwork: Network became ready, initializing network services");
		m_bNetworkInit = true;
		CString IPString;
		m_pNet->GetConfig()->GetIPAddress()->Format(&IPString);

		if (m_UDPMIDI)
		{
			m_UDPMIDI->Initialize();
		}

		if (m_pConfig->GetNetworkFTPEnabled()) {
			m_pFTPDaemon = new CFTPDaemon(FTPUSERNAME, FTPPASSWORD, m_pmDNSPublisher, m_pConfig);

			if (!m_pFTPDaemon->Initialize())
			{
				LOGERR("Failed to init FTP daemon");
				delete m_pFTPDaemon;
				m_pFTPDaemon = nullptr;
			}
			else 
			{
				LOGNOTE("FTP daemon initialized");
			}
		} else {
			LOGNOTE("FTP daemon not started (NetworkFTPEnabled=0)");
		}

		if (IPString.GetLength() > 0) {
            m_UI.LCDMessage("IP address is \n%s", (const char*)IPString);
        }

		m_pmDNSPublisher = new CmDNSPublisher (m_pNet);
		assert (m_pmDNSPublisher);
		
		if (!m_pmDNSPublisher->PublishService (m_pConfig->GetNetworkHostname(), CmDNSPublisher::ServiceTypeAppleMIDI,
						     5004))
		{
			LOGPANIC ("Cannot publish mdns service");
		}

		static constexpr const char *ServiceTypeFTP = "_ftp._tcp";
		static const char *ftpTxt[] = { "app=MiniDexed", nullptr };
		if (!m_pmDNSPublisher->PublishService (m_pConfig->GetNetworkHostname(), ServiceTypeFTP, 21, ftpTxt))
		{
			LOGPANIC ("Cannot publish mdns service");
		}

		if (m_pConfig->GetSyslogEnabled())
		{
			LOGNOTE ("Syslog server is enabled in configuration");
			CIPAddress ServerIP = m_pConfig->GetNetworkSyslogServerIPAddress();
			if (ServerIP.IsSet () && !ServerIP.IsNull ())
			{
				static const u16 usServerPort = 514;
				CString IPString;
				ServerIP.Format (&IPString);
				LOGNOTE ("Sending log messages to syslog server %s:%u",
					(const char *) IPString, (unsigned) usServerPort);

				new CSysLogDaemon (m_pNet, ServerIP, usServerPort);
			}
		}
		m_bNetworkReady = true;
	}

	if (m_bNetworkReady && !bNetIsRunning)
	{
		LOGNOTE("CMiniJV880::UpdateNetwork: Network disconnected");
		m_bNetworkReady = false;
		m_pmDNSPublisher->UnpublishService (m_pConfig->GetNetworkHostname());
		LOGNOTE("Network disconnected.");
	}
	else if (!m_bNetworkReady && bNetIsRunning)
	{
		LOGNOTE("CMiniJV880::UpdateNetwork: Network connection reestablished");
		m_bNetworkReady = true;
		
		if (!m_pmDNSPublisher->PublishService (m_pConfig->GetNetworkHostname(), CmDNSPublisher::ServiceTypeAppleMIDI,
						     5004))
		{
			LOGPANIC ("Cannot publish mdns service");
		}

		static constexpr const char *ServiceTypeFTP = "_ftp._tcp";
		static const char *ftpTxt[] = { "app=MiniJV880", nullptr };
		if (!m_pmDNSPublisher->PublishService (m_pConfig->GetNetworkHostname(), ServiceTypeFTP, 21, ftpTxt))
		{
			LOGPANIC ("Cannot publish mdns service");
		}
		
		m_bNetworkReady = true;
		
		LOGNOTE("Network connection reestablished.");

	}
}

bool CMiniJV880::InitNetwork()
{
	LOGNOTE("CMiniJV880::InitNetwork called");
	assert(m_pNet == nullptr);

	TNetDeviceType NetDeviceType = NetDeviceTypeUnknown;

	if (m_pConfig->GetNetworkEnabled())
	{
		LOGNOTE("CMiniJV880::InitNetwork: Network type set in configuration: %s", m_pConfig->GetNetworkType());

		if (strcmp(m_pConfig->GetNetworkType(), "wlan") == 0)
		{
			LOGNOTE("CMiniJV880::InitNetwork: Initializing WLAN");
			NetDeviceType = NetDeviceTypeWLAN;
			m_WLAN = new CBcm4343Device(WLANFirmwarePath);
			if (m_WLAN && m_WLAN->Initialize())
			{
				LOGNOTE("CMiniJV880::InitNetwork: WLAN initialized");
			}
			else
			{
				LOGERR("CMiniJV880::InitNetwork: Failed to initialize WLAN, maybe firmware files are missing?");
				delete m_WLAN; m_WLAN = nullptr;
				return false;
			}
		}
		else if (strcmp(m_pConfig->GetNetworkType(), "ethernet") == 0)
		{
			LOGNOTE("CMiniJV880::InitNetwork: Initializing Ethernet");
			NetDeviceType = NetDeviceTypeEthernet;
		}
		else 
		{
			LOGERR("CMiniJV880::InitNetwork: Network type is not set, please check your minidexed configuration file.");
			NetDeviceType = NetDeviceTypeUnknown;
		}
		
		if (NetDeviceType != NetDeviceTypeUnknown)
		{
			LOGNOTE("CMiniJV880::InitNetwork: Creating CNetSubSystem");
			if (m_pConfig->GetNetworkDHCP()) {
				m_pNet = new CNetSubSystem(0, 0, 0, 0, m_pConfig->GetNetworkHostname(), NetDeviceType);
            } else {
				m_pNet = new CNetSubSystem(
					m_pConfig->GetNetworkIPAddress().Get(),
					m_pConfig->GetNetworkSubnetMask().Get(),
					m_pConfig->GetNetworkDefaultGateway().Get(),
					m_pConfig->GetNetworkDNSServer().Get(),
					m_pConfig->GetNetworkHostname(),
					NetDeviceType
				);
            }
			if (!m_pNet || !m_pNet->Initialize(false)) // Check if m_pNet allocation succeeded
			{
				LOGERR("CMiniJV880::InitNetwork: Failed to initialize network subsystem");
				delete m_pNet; m_pNet = nullptr; // Clean up if failed
				delete m_WLAN; m_WLAN = nullptr; // Clean up WLAN if allocated
				return false; // Return false as network init failed
			} 

			if (NetDeviceType == NetDeviceTypeWLAN)
			{
				LOGNOTE("CMiniJV880::InitNetwork: Initializing WPASupplicant");
				m_WPASupplicant = new CWPASupplicant(WLANConfigFile); // Allocate m_WPASupplicant
				if (!m_WPASupplicant || !m_WPASupplicant->Initialize()) 
				{
					LOGERR("CMiniJV880::InitNetwork: Failed to initialize WPASupplicant, maybe wlan config is missing?"); 
					delete m_WPASupplicant; m_WPASupplicant = nullptr; // Clean up if failed
					// Continue without supplicant? Or return false? Decided to continue for now.
				}
			}
			m_pNetDevice = CNetDevice::GetNetDevice(NetDeviceType);

			// Allocate UDP MIDI device now that network might be up
			m_UDPMIDI = new CUDPMIDIDevice(this, m_pConfig); // Allocate m_UDPMIDI
			if (!m_UDPMIDI) {
				LOGERR("CMiniJV880::InitNetwork: Failed to allocate UDP MIDI device");
				// Clean up other network resources if needed, or handle error appropriately
			} 
		}
		LOGNOTE("CMiniJV880::InitNetwork: returning %d", m_pNet != nullptr);
		return m_pNet != nullptr;
	}
	else
	{
		LOGNOTE("CMiniJV880::InitNetwork: Network is not enabled in configuration");
		return false;
	}
}
