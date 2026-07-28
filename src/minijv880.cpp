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
    : CMultiCoreSupport(CMemorySystem::Get()),
      m_bankMappings(nullptr),
      m_bankMappingsCount(0),
      m_bankMappingsCapacity(0),
      m_currentExpansionRomIndex(static_cast<int>(pConfig->GetExpRom()) + 6),
      m_pConfig(pConfig),
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
    delete[] m_bankMappings;
    m_bankMappings = nullptr;

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
   m_currentExpansionRomIndex = static_cast<int>(m_pConfig->GetExpRom()) + 6;
   {
       const int initialBank = FindLowestBankForRom(m_currentExpansionRomIndex);
       m_currentBankNumber = initialBank >= 0 ? initialBank : 0;
   }
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

    RefreshMIDIPotSnapshot();
    m_UI.Process ();
    ProcessMIDISurfaceButtons();
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

  ScanUSBMIDIDevices();
}

void CMiniJV880::ScanUSBMIDIDevices()
{
  for (unsigned deviceIndex = 0; deviceIndex < MAX_USB_MIDI_DEVICES;
       ++deviceIndex)
  {
    char deviceName[16];
    snprintf(deviceName, sizeof(deviceName), "umidi%u", deviceIndex + 1);

    CUSBMIDIDevice *pDevice = static_cast<CUSBMIDIDevice *>(
        CDeviceNameService::Get()->GetDevice(deviceName, FALSE));
    if (pDevice == 0)
      continue;

    bool alreadyRegistered = false;
    for (unsigned slot = 0; slot < MAX_USB_MIDI_DEVICES; ++slot)
    {
      if (m_pMIDIDevices[slot] == pDevice)
      {
        alreadyRegistered = true;
        break;
      }
    }
    if (alreadyRegistered)
      continue;

    unsigned freeSlot = MAX_USB_MIDI_DEVICES;
    for (unsigned slot = 0; slot < MAX_USB_MIDI_DEVICES; ++slot)
    {
      if (m_pMIDIDevices[slot] == 0)
      {
        freeSlot = slot;
        break;
      }
    }

    if (freeSlot == MAX_USB_MIDI_DEVICES)
    {
      LOGERR("No free USB MIDI slot for %s", deviceName);
      continue;
    }

    m_pMIDIDevices[freeSlot] = pDevice;
    pDevice->RegisterPacketHandler(USBMIDIMessageHandler);
    pDevice->RegisterRemovedHandler(DeviceRemovedHandler, this);
    LOGNOTE("USB MIDI %s registered in slot %u", deviceName, freeSlot + 1);
  }
}

void CMiniJV880::USBMIDIMessageHandler(unsigned nCable, u8 *pPacket,
                                       unsigned nLength) {
  if (!pPacket || nLength == 0) return;
  s_pThis->midiParser.FeedUSBMIDIPacket(pPacket, nLength);
}

bool CMiniJV880::HandleMIDISurfaceButton(uint8_t number, bool pressed)
{
    if (!m_pConfig->GetMIDISurfaceButtonsEnabled()) return false;

    for (unsigned button = 0; button < 16; ++button)
    {
        const unsigned configured = m_pConfig->GetMIDISurfaceButton(button);
        if (configured == 0 || configured != number) continue;

        // Consume both the press and release so configured surface notes do
        // not leak through as musical Note On/Off messages.
        if (!pressed) return true;

        const uint16_t ledState = mcu.jv880_led_state;
        const bool patchMode = (ledState & (1u << 5)) != 0;
        const bool menuMode = (ledState & ((1u << 2) | (1u << 3) | (1u << 4))) != 0;

        if (button < 8)
        {
            if (menuMode)
            {
                m_UI.LCDMessage("MIDI surface\nExit menu first");
                return true;
            }

            if (patchMode)
            {
                if (button < 4)
                    QueueMIDISurfaceCommand(SurfaceCommandToneSwitch, button);
                else
                    QueueMIDISurfaceCommand(SurfaceCommandToneSelect, button - 4);
            }
            else
            {
                QueueMIDISurfaceCommand(SurfaceCommandPartToggle, button);
            }
            return true;
        }

        if (button < 12)
        {
            m_nMIDIPotBank = button - 7;
            m_UI.SetMIDIPotBank(m_nMIDIPotBank);
            m_UI.ClearPatchToneValues();
            ResetMIDIPotPickup();
            m_nMIDIPotPatchLoadedPotBank = 0;
            m_nMIDIPotSnapshotTick = 0;
            m_UI.LCDMessage("Pot Bank\n%u", m_nMIDIPotBank);
            return true;
        }

        switch (button)
        {
        case 12: SelectAdjacentExpansion(false); break;
        case 13: SelectAdjacentExpansion(true);  break;
        case 14: SelectAdjacentBank(false);      break;
        case 15: SelectAdjacentBank(true);       break;
        default: break;
        }
        return true;
    }

    return false;
}

void CMiniJV880::QueueMIDISurfaceCommand(uint8_t type, uint8_t index)
{
    const unsigned tail = m_nMIDISurfaceQueueTail.load(std::memory_order_relaxed);
    const unsigned next = (tail + 1) % MIDI_SURFACE_QUEUE_SIZE;
    if (next == m_nMIDISurfaceQueueHead.load(std::memory_order_acquire))
    {
        LOGNOTE("MIDI surface command queue full");
        return;
    }

    m_MIDISurfaceQueue[tail].type = type;
    m_MIDISurfaceQueue[tail].index = index;
    m_nMIDISurfaceQueueTail.store(next, std::memory_order_release);
}

bool CMiniJV880::DequeueMIDISurfaceCommand()
{
    const unsigned head = m_nMIDISurfaceQueueHead.load(std::memory_order_relaxed);
    if (head == m_nMIDISurfaceQueueTail.load(std::memory_order_acquire)) return false;

    m_ActiveMIDISurfaceCommand = m_MIDISurfaceQueue[head];
    m_nMIDISurfaceQueueHead.store((head + 1) % MIDI_SURFACE_QUEUE_SIZE,
                                  std::memory_order_release);
    m_nMIDISurfaceCommandStage = 0;
    m_nMIDISurfaceCursorSteps = 0;
    m_nMIDISurfaceEncoderAttempts = 0;
    m_nMIDISurfaceWaitStarted = CTimer::GetClockTicks();
    return true;
}

void CMiniJV880::StartMIDISurfacePulse(uint32_t buttonMask)
{
    mcu.mcu_button_pressed = buttonMask;
    m_bMIDISurfacePulseActive = true;
    m_bMIDISurfacePulseReleased = false;
    m_nMIDISurfacePulseDeadline = CTimer::GetClockTicks() + MIDI_SURFACE_PRESS_US;
}

bool CMiniJV880::ProcessMIDISurfacePulse()
{
    if (!m_bMIDISurfacePulseActive) return false;

    const uint32_t now = CTimer::GetClockTicks();
    if (static_cast<int32_t>(now - m_nMIDISurfacePulseDeadline) < 0) return true;

    if (!m_bMIDISurfacePulseReleased)
    {
        mcu.mcu_button_pressed = 0;
        m_bMIDISurfacePulseReleased = true;
        m_nMIDISurfacePulseDeadline = now + MIDI_SURFACE_RELEASE_US;
        return true;
    }

    m_bMIDISurfacePulseActive = false;
    return false;
}

void CMiniJV880::FinishMIDISurfaceCommand()
{
    mcu.mcu_button_pressed = 0;
    m_ActiveMIDISurfaceCommand.type = SurfaceCommandNone;
    m_ActiveMIDISurfaceCommand.index = 0;
    m_nMIDISurfaceCommandStage = 0;
    m_nMIDISurfaceCursorSteps = 0;
    m_nMIDISurfaceEncoderAttempts = 0;
}

bool CMiniJV880::LCDRowContains(unsigned row, const char *text) const
{
    if (row >= 2 || text == nullptr || *text == 0) return false;

    const uint8_t *lcdRow = mcu.lcd.LCD_Data + row * 40;
    const unsigned textLength = strlen(text);
    if (textLength > 40) return false;

    for (unsigned column = 0; column + textLength <= 40; ++column)
    {
        bool match = true;
        for (unsigned index = 0; index < textLength; ++index)
        {
            uint8_t ch = lcdRow[column + index];
            if (ch == 0x09) ch = '|';
            if (ch != static_cast<uint8_t>(text[index]))
            {
                match = false;
                break;
            }
        }
        if (match) return true;
    }

    return false;
}

int CMiniJV880::FindPerformancePartColumn(unsigned part) const
{
    if (part < 1 || part > 8) return -1;

    const uint8_t *row = mcu.lcd.LCD_Data;
    auto isSeparator = [](uint8_t ch) {
        return ch == 0x09 || ch == '|';
    };

    // Performance Play/Mute renders |1|2|3|4|5|6|7|8|. A muted
    // part is represented by '*' (and may briefly blink as a space),
    // while the separators and slot columns remain stable.
    for (int firstSeparator = 0; firstSeparator + 16 < 40; ++firstSeparator)
    {
        bool valid = true;
        for (int separator = 0; separator <= 8; ++separator)
        {
            if (!isSeparator(row[firstSeparator + separator * 2]))
            {
                valid = false;
                break;
            }
        }
        if (!valid) continue;

        for (int slot = 0; slot < 8; ++slot)
        {
            const uint8_t ch = row[firstSeparator + 1 + slot * 2];
            if (ch != static_cast<uint8_t>('1' + slot)
                && ch != '*' && ch != ' ')
            {
                valid = false;
                break;
            }
        }

        if (valid) return firstSeparator + 1 + static_cast<int>(part - 1) * 2;
    }

    return -1;
}

void CMiniJV880::ProcessMIDISurfaceButtons()
{
    if (!m_pConfig->GetMIDISurfaceButtonsEnabled()) return;
    if (ProcessMIDISurfacePulse()) return;

    if (m_ActiveMIDISurfaceCommand.type == SurfaceCommandNone
        && !DequeueMIDISurfaceCommand()) return;

    static const uint8_t toneSwitchButtons[4] = {
        MCU_BUTTON_MUTE, MCU_BUTTON_MONITOR,
        MCU_BUTTON_COMPARE, MCU_BUTTON_ENTER
    };

    const uint32_t now = CTimer::GetClockTicks();
    const uint16_t ledState = mcu.jv880_led_state;
    const bool patchMode = (ledState & (1u << 5)) != 0;
    const bool editMode = (ledState & (1u << 1)) != 0;
    const bool menuMode = (ledState & ((1u << 2) | (1u << 3) | (1u << 4))) != 0;

    switch (m_ActiveMIDISurfaceCommand.type)
    {
    case SurfaceCommandToneSwitch:
        if (m_nMIDISurfaceCommandStage == 0)
        {
            if (!patchMode || menuMode || m_ActiveMIDISurfaceCommand.index >= 4)
            {
                FinishMIDISurfaceCommand();
                return;
            }
            StartMIDISurfacePulse(1u << toneSwitchButtons[m_ActiveMIDISurfaceCommand.index]);
            m_nMIDISurfaceCommandStage = 1;
            return;
        }
        FinishMIDISurfaceCommand();
        return;

    case SurfaceCommandToneSelect:
        if (!patchMode || menuMode || m_ActiveMIDISurfaceCommand.index >= 4)
        {
            FinishMIDISurfaceCommand();
            return;
        }

        // Enter Patch Edit first. The JV-880 opens Patch:Common, so the
        // command must then move the cursor to the upper selector and turn
        // the DATA encoder to Patch:Tone before using Tone Select + Switch.
        if (m_nMIDISurfaceCommandStage == 0)
        {
            if (!editMode)
            {
                StartMIDISurfacePulse(1u << MCU_BUTTON_EDIT);
                m_nMIDISurfaceCommandStage = 1;
                m_nMIDISurfaceWaitStarted = now;
                return;
            }
            m_nMIDISurfaceCommandStage = 2;
            m_nMIDISurfaceWaitStarted = now;
        }

        if (m_nMIDISurfaceCommandStage == 1)
        {
            if (!editMode)
            {
                if (now - m_nMIDISurfaceWaitStarted >= MIDI_SURFACE_MODE_WAIT_US)
                    FinishMIDISurfaceCommand();
                return;
            }
            m_nMIDISurfaceCommandStage = 2;
            m_nMIDISurfaceWaitStarted = now;
        }

        if (m_nMIDISurfaceCommandStage == 3)
        {
            // Cursor pulse completed; inspect the new cursor position.
            m_nMIDISurfaceCommandStage = 2;
            m_nMIDISurfaceWaitStarted = now;
        }

        if (m_nMIDISurfaceCommandStage == 2)
        {
            if (LCDRowContains(0, "Patch:Tone"))
            {
                m_nMIDISurfaceCommandStage = 6;
            }
            else
            {
                const int cursorRow = mcu.lcd.LCD_DD_RAM / 0x40;
                if (cursorRow != 0)
                {
                    if (m_nMIDISurfaceCursorSteps >= MIDI_SURFACE_MAX_CURSOR_STEPS)
                    {
                        FinishMIDISurfaceCommand();
                        return;
                    }

                    StartMIDISurfacePulse(1u << MCU_BUTTON_CURSOR_L);
                    ++m_nMIDISurfaceCursorSteps;
                    m_nMIDISurfaceCommandStage = 3;
                    return;
                }

                if (!LCDRowContains(0, "Patch:Common"))
                {
                    if (now - m_nMIDISurfaceWaitStarted >= MIDI_SURFACE_MODE_WAIT_US)
                        FinishMIDISurfaceCommand();
                    return;
                }

                // Try the normal DATA direction first. If this firmware build
                // maps the encoder direction oppositely, stage 4 retries once
                // in the other direction.
                mcu.MCU_EncoderTrigger(1);
                m_nMIDISurfaceEncoderAttempts = 1;
                m_nMIDISurfaceCommandStage = 4;
                m_nMIDISurfaceWaitStarted = now;
                return;
            }
        }

        if (m_nMIDISurfaceCommandStage == 4)
        {
            if (LCDRowContains(0, "Patch:Tone"))
            {
                m_nMIDISurfaceCommandStage = 6;
            }
            else if (now - m_nMIDISurfaceWaitStarted >= MIDI_SURFACE_ENCODER_WAIT_US)
            {
                if (m_nMIDISurfaceEncoderAttempts == 1)
                {
                    mcu.MCU_EncoderTrigger(0);
                    m_nMIDISurfaceEncoderAttempts = 2;
                    m_nMIDISurfaceWaitStarted = now;
                    return;
                }

                FinishMIDISurfaceCommand();
                return;
            }
            else
            {
                return;
            }
        }

        if (m_nMIDISurfaceCommandStage == 6)
        {
            const uint32_t mask = (1u << MCU_BUTTON_TONE_SELECT)
                | (1u << toneSwitchButtons[m_ActiveMIDISurfaceCommand.index]);
            StartMIDISurfacePulse(mask);
            m_nMIDISurfaceCommandStage = 7;
            return;
        }

        FinishMIDISurfaceCommand();
        return;

    case SurfaceCommandPartToggle:
        if (patchMode || menuMode || m_ActiveMIDISurfaceCommand.index >= 8)
        {
            FinishMIDISurfaceCommand();
            return;
        }

        if (m_nMIDISurfaceCommandStage == 0)
        {
            if (editMode)
            {
                StartMIDISurfacePulse(1u << MCU_BUTTON_EDIT);
                m_nMIDISurfaceCommandStage = 1;
                m_nMIDISurfaceWaitStarted = now;
                return;
            }
            m_nMIDISurfaceCommandStage = 2;
            m_nMIDISurfaceWaitStarted = now;
        }

        if (m_nMIDISurfaceCommandStage == 1)
        {
            if (editMode)
            {
                if (now - m_nMIDISurfaceWaitStarted >= MIDI_SURFACE_MODE_WAIT_US)
                    FinishMIDISurfaceCommand();
                return;
            }
            m_nMIDISurfaceCommandStage = 2;
            m_nMIDISurfaceWaitStarted = now;
        }

        if (m_nMIDISurfaceCommandStage == 3)
            m_nMIDISurfaceCommandStage = 2;

        if (m_nMIDISurfaceCommandStage == 2)
        {
            const unsigned part = m_ActiveMIDISurfaceCommand.index + 1;
            const int targetColumn = FindPerformancePartColumn(part);
            if (targetColumn < 0)
            {
                if (now - m_nMIDISurfaceWaitStarted >= MIDI_SURFACE_MODE_WAIT_US)
                    FinishMIDISurfaceCommand();
                return;
            }

            const int cursorRow = mcu.lcd.LCD_DD_RAM / 0x40;
            const int cursorColumn = mcu.lcd.LCD_DD_RAM % 0x40;
            if (cursorRow == 0 && cursorColumn == targetColumn)
            {
                StartMIDISurfacePulse(1u << MCU_BUTTON_MUTE);
                m_nMIDISurfaceCommandStage = 4;
                return;
            }

            if (m_nMIDISurfaceCursorSteps >= MIDI_SURFACE_MAX_CURSOR_STEPS)
            {
                FinishMIDISurfaceCommand();
                return;
            }

            StartMIDISurfacePulse(1u << MCU_BUTTON_CURSOR_R);
            ++m_nMIDISurfaceCursorSteps;
            m_nMIDISurfaceCommandStage = 3;
            return;
        }

        FinishMIDISurfaceCommand();
        return;

    default:
        FinishMIDISurfaceCommand();
        return;
    }
}

int CMiniJV880::FindRomForBank(int bankNumber) const
{
    for (unsigned index = 0; index < m_bankMappingsCount; ++index)
        if (m_bankMappings[index].bankNumber == bankNumber)
            return m_bankMappings[index].romIndex;
    return -1;
}

int CMiniJV880::FindLowestBankForRom(int romIndex) const
{
    int result = -1;
    for (unsigned index = 0; index < m_bankMappingsCount; ++index)
    {
        const BankMapping &mapping = m_bankMappings[index];
        if (mapping.romIndex != romIndex) continue;
        if (result < 0 || mapping.bankNumber < result) result = mapping.bankNumber;
    }
    return result;
}

int CMiniJV880::GetCurrentOrPendingBank() const
{
    const int pending = m_nPendingBankSwitch.load(std::memory_order_acquire);
    return pending >= 0 && pending != 0xFF ? pending : m_currentBankNumber;
}

void CMiniJV880::QueuePatchBankSwitch(int bankNumber)
{
    if (FindRomForBank(bankNumber) < 0)
    {
        LOGNOTE("Bank %d not found in mapping", bankNumber);
        return;
    }

    m_nPendingBankSwitch.store(bankNumber, std::memory_order_release);
    m_nBankSwitchTimestamp.store(CTimer::GetClockTicks(), std::memory_order_release);
}

void CMiniJV880::SelectAdjacentExpansion(bool up)
{
    if (m_bankMappingsCount == 0) return;

    int currentRom = m_currentExpansionRomIndex;
    const int pendingRom = FindRomForBank(GetCurrentOrPendingBank());
    if (pendingRom >= 0) currentRom = pendingRom;

    int lowestRom = -1;
    int highestRom = -1;
    int targetRom = -1;
    for (unsigned index = 0; index < m_bankMappingsCount; ++index)
    {
        const int rom = m_bankMappings[index].romIndex;
        if (lowestRom < 0 || rom < lowestRom) lowestRom = rom;
        if (highestRom < 0 || rom > highestRom) highestRom = rom;

        if (up && rom > currentRom && (targetRom < 0 || rom < targetRom))
            targetRom = rom;
        if (!up && rom < currentRom && (targetRom < 0 || rom > targetRom))
            targetRom = rom;
    }

    if (targetRom < 0) targetRom = up ? lowestRom : highestRom;
    const int targetBank = FindLowestBankForRom(targetRom);
    if (targetBank < 0) return;

    QueuePatchBankSwitch(targetBank);
    m_UI.LCDMessage("Expansion %02d\nBank %02d", targetRom - 6, targetBank);
}

void CMiniJV880::SelectAdjacentBank(bool up)
{
    if (m_bankMappingsCount == 0) return;

    const int baseBank = GetCurrentOrPendingBank();
    int currentRom = FindRomForBank(baseBank);
    if (currentRom < 0) currentRom = m_currentExpansionRomIndex;

    int lowestBank = -1;
    int highestBank = -1;
    int targetBank = -1;
    for (unsigned index = 0; index < m_bankMappingsCount; ++index)
    {
        const BankMapping &mapping = m_bankMappings[index];
        if (mapping.romIndex != currentRom) continue;

        const int bank = mapping.bankNumber;
        if (lowestBank < 0 || bank < lowestBank) lowestBank = bank;
        if (highestBank < 0 || bank > highestBank) highestBank = bank;

        if (up && bank > baseBank && (targetBank < 0 || bank < targetBank))
            targetBank = bank;
        if (!up && bank < baseBank && (targetBank < 0 || bank > targetBank))
            targetBank = bank;
    }

    if (targetBank < 0) targetBank = up ? lowestBank : highestBank;
    if (targetBank < 0) return;

    QueuePatchBankSwitch(targetBank);
    m_UI.LCDMessage("Expansion %02d\nBank %02d", currentRom - 6, targetBank);
}

void CMiniJV880::ResetMIDIPotPickup()
{
    memset(m_bMIDIPotLastPhysicalValid, 0, sizeof m_bMIDIPotLastPhysicalValid);
    memset(m_bMIDIPotPickedUp, 0, sizeof m_bMIDIPotPickedUp);
    memset(m_bMIDIPotSwitchArmed, 0, sizeof m_bMIDIPotSwitchArmed);
}

void CMiniJV880::SendJV880DT1(uint8_t a0, uint8_t a1, uint8_t a2,
                              uint8_t a3, uint8_t value)
{
    uint8_t message[12] = {
        0xF0, 0x41, 0x10, 0x46, 0x12,
        a0, a1, a2, a3, static_cast<uint8_t>(value & 0x7F), 0, 0xF7
    };

    const unsigned sum = a0 + a1 + a2 + a3 + (value & 0x7F);
    message[10] = static_cast<uint8_t>((128 - (sum & 0x7F)) & 0x7F);
    mcu.postMidiSC55(message, sizeof message);
}

void CMiniJV880::SendJV880DT1NibblePair(uint8_t a0, uint8_t a1,
                                        uint8_t a2, uint8_t a3,
                                        uint8_t value)
{
    const uint8_t highNibble = static_cast<uint8_t>((value >> 4) & 0x0F);
    const uint8_t lowNibble = static_cast<uint8_t>(value & 0x0F);
    uint8_t message[13] = {
        0xF0, 0x41, 0x10, 0x46, 0x12,
        a0, a1, a2, a3, highNibble, lowNibble, 0, 0xF7
    };

    const unsigned sum = a0 + a1 + a2 + a3 + highNibble + lowNibble;
    message[11] = static_cast<uint8_t>((128 - (sum & 0x7F)) & 0x7F);
    mcu.postMidiSC55(message, sizeof message);
}

bool CMiniJV880::FindCurrentPatchSource(char &bank, unsigned &patchIndex,
                                         const uint8_t *&patchData) const
{
    bank = 0;
    patchIndex = 0;
    patchData = nullptr;

    // The native Patch Play display contains an identifier such as I07:,
    // A01:, B64: or C12:. Read that stable identifier instead of relying on
    // the Working Patch NVRAM area, which the original firmware does not
    // refresh on every normal patch selection in Mini-JV880pi.
    for (unsigned row = 0; row < 2; ++row)
    {
        const uint8_t *lcdRow = mcu.lcd.LCD_Data + row * 40;
        for (unsigned col = 0; col + 3 < 40; ++col)
        {
            const char candidateBank = static_cast<char>(lcdRow[col]);
            if (candidateBank != 'I' && candidateBank != 'A'
                && candidateBank != 'B' && candidateBank != 'C')
                continue;

            const uint8_t tens = lcdRow[col + 1];
            const uint8_t units = lcdRow[col + 2];
            if (tens < '0' || tens > '9' || units < '0' || units > '9'
                || lcdRow[col + 3] != ':')
                continue;

            const unsigned displayedNumber = (tens - '0') * 10u
                + (units - '0');
            if (displayedNumber < 1 || displayedNumber > 64)
                continue;

            bank = candidateBank;
            patchIndex = displayedNumber - 1;

            switch (bank)
            {
            case 'I':
                patchData = &mcu.nvram[NVRAM_PATCH_INTERNAL
                    + patchIndex * PATCH_SIZE];
                return true;
            case 'C':
                patchData = &mcu.cardram[CARDRAM_PATCH_INTERNAL
                    + patchIndex * PATCH_SIZE];
                return true;
            case 'A':
                patchData = &mcu.rom2[ROM2_PATCH_PRESET_A
                    + patchIndex * PATCH_SIZE];
                return true;
            case 'B':
                patchData = &mcu.rom2[ROM2_PATCH_PRESET_B
                    + patchIndex * PATCH_SIZE];
                return true;
            default:
                break;
            }
        }
    }

    return false;
}

void CMiniJV880::RefreshMIDIPotSnapshot()
{
    if (!m_pConfig->GetMIDIPotsEnabled() || mcu.mcu.pc == 0)
        return;

    const uint32_t now = CTimer::GetClockTicks();
    if (m_nMIDIPotSnapshotTick != 0
        && now - m_nMIDIPotSnapshotTick < MIDI_POT_SNAPSHOT_INTERVAL_US)
        return;
    m_nMIDIPotSnapshotTick = now;

    const bool patchMode = (mcu.jv880_led_state & (1u << 5)) != 0;
    const bool modeChanged = !m_bMIDIPotModeValid
        || patchMode != m_bMIDIPotLastPatchMode;

    if (modeChanged)
    {
        ResetMIDIPotPickup();
        memset(m_nMIDIPotWriteTick, 0, sizeof m_nMIDIPotWriteTick);
        m_bMIDIPotIdentityValid = false;
        m_bMIDIPotPatchSourceValid = false;
        memset(m_bMIDIPotPatchCacheValid, 0,
               sizeof m_bMIDIPotPatchCacheValid);
        m_bMIDIPotModeValid = true;
        m_bMIDIPotLastPatchMode = patchMode;

        if (patchMode)
            m_UI.ClearPatchToneValues();
        else
            m_UI.ClearPerformancePartValues();
    }

    auto updateTarget = [this, now](unsigned row, unsigned index,
                                    uint8_t value, bool patchValue) {
        if (row >= 4 || index >= 8) return;
        value &= 0x7F;

        const bool writePending = m_nMIDIPotWriteTick[row][index] != 0
            && now - m_nMIDIPotWriteTick[row][index] < MIDI_POT_WRITE_HOLD_US;
        if (writePending && m_bMIDIPotTargetValid[row][index]
            && value != m_nMIDIPotTargets[row][index])
            return;

        if (m_nMIDIPotWriteTick[row][index] != 0
            && (!m_bMIDIPotTargetValid[row][index]
                || value == m_nMIDIPotTargets[row][index]))
            m_nMIDIPotWriteTick[row][index] = 0;

        if (m_bMIDIPotTargetValid[row][index]
            && m_nMIDIPotTargets[row][index] != value
            && !writePending)
        {
            m_bMIDIPotPickedUp[row][index] = false;
            m_bMIDIPotSwitchArmed[row][index] = false;
            m_bMIDIPotLastPhysicalValid[row][index] = false;
        }

        m_nMIDIPotTargets[row][index] = value;
        m_bMIDIPotTargetValid[row][index] = true;
        if (patchValue)
            m_UI.SetPatchToneValue(row, index, value);
        else
            m_UI.SetPerformancePartValue(row, index, value);
    };

    if (!patchMode)
    {
        const uint8_t *identity = &mcu.sram[SRAM_TEMP_PERF_OFFSET];
        const bool identityChanged = !m_bMIDIPotIdentityValid
            || memcmp(m_nMIDIPotLastIdentity, identity,
                      sizeof m_nMIDIPotLastIdentity) != 0;

        if (identityChanged)
        {
            ResetMIDIPotPickup();
            memset(m_nMIDIPotWriteTick, 0, sizeof m_nMIDIPotWriteTick);
            memcpy(m_nMIDIPotLastIdentity, identity,
                   sizeof m_nMIDIPotLastIdentity);
            m_bMIDIPotIdentityValid = true;
            m_UI.ClearPerformancePartValues();
        }

        for (unsigned part = 0; part < 8; ++part)
        {
            const unsigned base = SRAM_TEMP_PERF_OFFSET + PERF_COMMON_SIZE
                + part * PERF_PART_SIZE;
            updateTarget(0, part, mcu.sram[base + 17], false);
            updateTarget(1, part, mcu.sram[base + 18], false);
            updateTarget(2, part, mcu.sram[base + 19], false);
            updateTarget(3, part, mcu.sram[base + 20], false);
        }
        return;
    }

    m_UI.SetMIDIPotBank(m_nMIDIPotBank);

    char patchBank = 0;
    unsigned patchIndex = 0;
    const uint8_t *patchData = nullptr;
    if (!FindCurrentPatchSource(patchBank, patchIndex, patchData))
    {
        // During DT1 reception the native firmware temporarily replaces the
        // Patch identifier with "Now bulk receiving". Keep the current table
        // and pickup targets until the normal Patch Play line returns.
        return;
    }

    const bool sourceChanged = !m_bMIDIPotPatchSourceValid
        || patchBank != m_cMIDIPotPatchBank
        || patchIndex != m_nMIDIPotPatchIndex
        || m_currentBankNumber != m_nMIDIPotPatchMappingBank;

    static const unsigned bankOffsets[2][4] = {
        { 67, 68, 82, 83 },
        { 74, 76, 53, 52 }
    };

    if (sourceChanged)
    {
        ResetMIDIPotPickup();
        memset(m_nMIDIPotWriteTick, 0, sizeof m_nMIDIPotWriteTick);
        memset(m_bMIDIPotPatchCacheValid, 0,
               sizeof m_bMIDIPotPatchCacheValid);

        // Load both implemented pot banks at once. This keeps temporary MIDI
        // edits visible when moving between Bank 1 and Bank 2, while a real
        // Patch change still replaces the entire cache with the new Patch.
        for (unsigned bankIndex = 0; bankIndex < 2; ++bankIndex)
        {
            for (unsigned tone = 0; tone < 4; ++tone)
            {
                const unsigned base = PATCH_COMMON_SIZE
                    + tone * PATCH_TONE_SIZE;
                for (unsigned row = 0; row < 4; ++row)
                {
                    uint8_t value = patchData[base + bankOffsets[bankIndex][row]];
                    if (bankIndex == 1 && row == 2)
                        value &= 0x7F;
                    m_nMIDIPotPatchCache[bankIndex][row][tone] = value;
                    m_bMIDIPotPatchCacheValid[bankIndex][row][tone] = true;
                }
            }
        }

        m_bMIDIPotPatchSourceValid = true;
        m_cMIDIPotPatchBank = patchBank;
        m_nMIDIPotPatchIndex = patchIndex;
        m_nMIDIPotPatchMappingBank = m_currentBankNumber;
        m_nMIDIPotPatchLoadedPotBank = 0;
    }

    if (m_nMIDIPotBank < 1 || m_nMIDIPotBank > 2)
    {
        if (m_nMIDIPotPatchLoadedPotBank != m_nMIDIPotBank)
        {
            m_UI.ClearPatchToneValues();
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned tone = 0; tone < 4; ++tone)
                    m_bMIDIPotTargetValid[row][tone] = false;
            m_nMIDIPotPatchLoadedPotBank = m_nMIDIPotBank;
        }
        return;
    }

    if (!sourceChanged && m_nMIDIPotPatchLoadedPotBank == m_nMIDIPotBank)
        return;

    ResetMIDIPotPickup();
    memset(m_nMIDIPotWriteTick, 0, sizeof m_nMIDIPotWriteTick);
    m_UI.ClearPatchToneValues();
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned tone = 0; tone < 4; ++tone)
            m_bMIDIPotTargetValid[row][tone] = false;

    const unsigned cacheBank = m_nMIDIPotBank - 1;
    for (unsigned tone = 0; tone < 4; ++tone)
    {
        for (unsigned row = 0; row < 4; ++row)
        {
            if (!m_bMIDIPotPatchCacheValid[cacheBank][row][tone])
                continue;
            updateTarget(row, tone,
                m_nMIDIPotPatchCache[cacheBank][row][tone], true);
        }
    }
    m_nMIDIPotPatchLoadedPotBank = m_nMIDIPotBank;
}

bool CMiniJV880::HandleMIDIPotCC(uint8_t channel, uint8_t ccNumber,
                                 uint8_t value)
{
    if (!m_pConfig->GetMIDIPotsEnabled()) return false;

    const unsigned configuredChannel = m_pConfig->GetMIDIPotCh();
    if (configuredChannel == 0) return false;
    if (configuredChannel != 17 && configuredChannel - 1 != channel)
        return false;

    unsigned row = 4;
    unsigned index = 8;
    for (unsigned candidateRow = 0; candidateRow < 4 && row == 4;
         ++candidateRow)
    {
        for (unsigned candidateIndex = 0; candidateIndex < 8;
             ++candidateIndex)
        {
            const unsigned configuredCC =
                m_pConfig->GetMIDIPotControl(candidateRow, candidateIndex);
            if (configuredCC != 0 && configuredCC == ccNumber)
            {
                row = candidateRow;
                index = candidateIndex;
                break;
            }
        }
    }
    if (row >= 4 || index >= 8) return false;

    // CC 32 remains the JV-880 Bank Select LSB. It is never consumed by the
    // generic pot layer, even if it is accidentally assigned in the INI.
    if (ccNumber == 32) return false;

    const bool patchMode = (mcu.jv880_led_state & (1u << 5)) != 0;
    if (patchMode && (index >= 4 || m_nMIDIPotBank < 1 || m_nMIDIPotBank > 2))
        return true;

    if (!m_bMIDIPotTargetValid[row][index])
    {
        m_nMIDIPotSnapshotTick = 0;
        RefreshMIDIPotSnapshot();
        if (!m_bMIDIPotTargetValid[row][index]) return true;
    }

    value &= 0x7F;
    const uint8_t target = m_nMIDIPotTargets[row][index];
    const uint32_t now = CTimer::GetClockTicks();

    // Coarse and Fine Tune have narrower raw ranges than a MIDI controller.
    // Map the full 0..127 travel before pickup so the end stops reach the
    // complete JV-880 ranges: Coarse 16..112 (-48..+48), Fine 14..114 (-50..+50).
    if (!patchMode && row == 2)
        value = static_cast<uint8_t>(16u + (static_cast<unsigned>(value) * 96u + 63u) / 127u);
    else if (!patchMode && row == 3)
        value = static_cast<uint8_t>(14u + (static_cast<unsigned>(value) * 100u + 63u) / 127u);

    bool pickupReached = m_bMIDIPotPickedUp[row][index];
    if (!pickupReached)
    {
        const int difference = static_cast<int>(value) - static_cast<int>(target);
        const bool closeEnough = difference >= -MIDI_POT_PICKUP_TOLERANCE
            && difference <= MIDI_POT_PICKUP_TOLERANCE;

        if (!m_bMIDIPotLastPhysicalValid[row][index])
        {
            m_nMIDIPotLastPhysical[row][index] = value;
            m_bMIDIPotLastPhysicalValid[row][index] = true;
            pickupReached = closeEnough;
        }
        else
        {
            const uint8_t previous = m_nMIDIPotLastPhysical[row][index];
            m_nMIDIPotLastPhysical[row][index] = value;
            const bool crossed = (previous < target && value >= target)
                || (previous > target && value <= target);
            pickupReached = closeEnough || crossed;
        }

        if (!pickupReached)
            return true;
        m_bMIDIPotPickedUp[row][index] = true;
    }
    else
    {
        m_nMIDIPotLastPhysical[row][index] = value;
        m_bMIDIPotLastPhysicalValid[row][index] = true;
    }

    if (patchMode)
    {
        static const uint8_t bank1Parameters[4] = { 0x5C, 0x5E, 0x71, 0x72 };
        static const uint8_t bank2Parameters[4] = { 0x69, 0x6B, 0x4B, 0x4A };
        const uint8_t parameter = m_nMIDIPotBank == 1
            ? bank1Parameters[row] : bank2Parameters[row];

        // DT1 updates the temporary Patch used by the emulated JV-880. Keep
        // the displayed target locally; the stored source Patch is re-read
        // only when the selected Patch or pot bank actually changes.
        if (m_nMIDIPotBank == 1 && row == 1)
            SendJV880DT1NibblePair(0x00, 0x08,
                static_cast<uint8_t>(0x28 + index), parameter, value);
        else
            SendJV880DT1(0x00, 0x08,
                static_cast<uint8_t>(0x28 + index), parameter, value);

        const unsigned cacheBank = m_nMIDIPotBank - 1;
        m_nMIDIPotPatchCache[cacheBank][row][index] = value;
        m_bMIDIPotPatchCacheValid[cacheBank][row][index] = true;
        m_UI.SetPatchToneValue(row, index, value);
    }
    else
    {
        const unsigned sramBase = SRAM_TEMP_PERF_OFFSET + PERF_COMMON_SIZE
            + index * PERF_PART_SIZE;
        mcu.sram[sramBase + 17 + row] = value & 0x7F;
        SendJV880DT1(0x00, 0x00, static_cast<uint8_t>(0x18 + index),
                     static_cast<uint8_t>(0x19 + row), value);
        m_UI.SetPerformancePartValue(row, index, value);
    }

    m_nMIDIPotTargets[row][index] = value;
    m_bMIDIPotTargetValid[row][index] = true;
    m_nMIDIPotWriteTick[row][index] = now;
    return true;
}

void CMiniJV880::TrackPerformancePartValues(const uint8_t *pData, uint8_t nLength)
{
    if (pData == nullptr || nLength == 0) return;

    const uint8_t status = pData[0];
    if ((status & 0xF0) == 0xB0 && nLength == 3)
    {
        const unsigned part = status & 0x0F;
        if (part < 8)
        {
            const uint8_t controller = pData[1] & 0x7F;
            const uint8_t value = pData[2] & 0x7F;
            if (controller == 7)       m_UI.SetPerformancePartValue(0, part, value);
            else if (controller == 10) m_UI.SetPerformancePartValue(1, part, value);
        }
        return;
    }

    if ((status & 0xF0) == 0xC0 && nLength >= 2)
    {
        ResetMIDIPotPickup();
        m_bMIDIPotIdentityValid = false;
        m_bMIDIPotPatchSourceValid = false;
        memset(m_bMIDIPotPatchCacheValid, 0,
               sizeof m_bMIDIPotPatchCacheValid);
        m_nMIDIPotSnapshotTick = 0;
        return;
    }

    // Roland JV-880 DT1: F0 41 dev 46 12 aa aa aa aa data... checksum F7
    if (nLength < 12 || pData[0] != 0xF0 || pData[1] != 0x41
        || pData[3] != 0x46 || pData[4] != 0x12 || pData[nLength - 1] != 0xF7)
        return;

    uint8_t address[4] = { pData[5], pData[6], pData[7], pData[8] };
    const unsigned dataEnd = nLength - 2;
    for (unsigned offset = 9; offset < dataEnd; ++offset)
    {
        const uint8_t value = pData[offset] & 0x7F;
        if (address[0] == 0x00 && address[1] == 0x00
            && address[2] >= 0x18 && address[2] <= 0x1F)
        {
            const unsigned part = address[2] - 0x18;
            if (address[3] >= 0x19 && address[3] <= 0x1C)
                m_UI.SetPerformancePartValue(address[3] - 0x19, part, value);
        }
        address[3] = (address[3] + 1) & 0x7F;
    }
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
    TrackPerformancePartValues(pData, nLength);

    auto MIDIButtonChannelMatches = [this](uint8_t channel) {
        if (m_UI.m_nMIDIButtonChannel == 0) return false;
        if (m_UI.m_nMIDIButtonChannel == 17) return true;
        return (m_UI.m_nMIDIButtonChannel - 1) == channel;
    };

    // ===== Priority 1: UI buttons by MIDI Note On/Off =====
    // Note On with velocity > 0 presses the virtual button.
    // Note Off, or Note On with velocity 0, releases it.
    if (m_UI.m_bMIDIButtonsUseNotes
        && ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
        && nLength == 3
        && MIDIButtonChannelMatches(status & 0x0F))
    {
        const uint8_t note = pData[1] & 0x7F;
        const bool pressed = (status & 0xF0) == 0x90 && pData[2] != 0;

        if (HandleMIDISurfaceButton(note, pressed)) return;

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

    // ===== Priority 2: Generic pots/faders with pickup =====
    // CC 32 is explicitly reserved inside HandleMIDIPotCC for Bank Select LSB.
    if ((status & 0xF0) == 0xB0 && nLength == 3
        && HandleMIDIPotCC(status & 0x0F, pData[1] & 0x7F, pData[2] & 0x7F))
    {
        return;
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

    // ===== Priority 5: Modulation (CC 1) =====
    if ((status & 0xF0) == 0xB0 && nLength == 3 && pData[1] == 1) {
        mcu.postMidiSC55(pData, nLength);
        return;
    }

    // ===== Priority 6: Bank Switch (CC 0 MSB and CC 32 LSB) =====
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
            m_nPendingBankSwitch.store(lsb, std::memory_order_release);
            m_nBankSwitchTimestamp.store(CTimer::GetClockTicks(), std::memory_order_release);
            return;
        }
    }

    // ===== Priority 7: UI Control Change messages =====
    // In Notes mode only the relative MIDI encoder remains on CC.
    // In legacy CC mode the buttons, Up/Down and NVRAM command also use CC.
    if ((status & 0xF0) == 0xB0 && nLength == 3
        && MIDIButtonChannelMatches(status & 0x0F))
    {
        const uint8_t ccNumber = pData[1] & 0x7F;
        const uint8_t ccValue = pData[2] & 0x7F;

        if (!m_UI.m_bMIDIButtonsUseNotes
            && HandleMIDISurfaceButton(ccNumber, ccValue < 64)) return;

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
  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(pContext);
  assert(pThis != 0);

  for (unsigned slot = 0; slot < MAX_USB_MIDI_DEVICES; ++slot)
  {
    if (pDevice == pThis->m_pMIDIDevices[slot])
    {
      pThis->m_pMIDIDevices[slot] = 0;
      LOGNOTE("USB MIDI device removed from slot %u", slot + 1);
      return;
    }
  }

  LOGERR("Removed USB MIDI device was not registered");
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
    m_bMIDIPotModeValid = false;
    m_bMIDIPotIdentityValid = false;
    m_bMIDIPotPatchSourceValid = false;
    memset(m_bMIDIPotPatchCacheValid, 0,
           sizeof m_bMIDIPotPatchCacheValid);
    m_nMIDIPotSnapshotTick = 0;
    ResetMIDIPotPickup();

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
