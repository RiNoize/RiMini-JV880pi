//
// minidexed.h
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
#ifndef _minijv880_h
#define _minijv880_h

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

#include "config.h"
#include "userinterface.h"
#include "midi.h"
#include "emulator/mcu.h"
#include <circle/gpiomanager.h>
#include <circle/i2cmaster.h>
#include <circle/interrupt.h>
#include <circle/multicore.h>
#include <circle/screen.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/spimaster.h>
#include <circle/spinlock.h>
#include <circle/types.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>
#include "net/mdnspublisher.h"
#include "udpmididevice.h"
#include "net/ftpdaemon.h"
#include <circle/usb/usbmidi.h>
#include <fatfs/ff.h>
#include <stdint.h>
#include <circle/serial.h>
#include <atomic>

class CMiniJV880 : public CMultiCoreSupport {
public:
  CMiniJV880(CConfig *pConfig, CInterruptSystem *pInterrupt,
             CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster,
             FATFS *pFileSystem, CScreenDevice *mScreenUnbuffered,
             CWriteBufferDevice *pHDMIScreen);
  ~CMiniJV880(void);

  bool Initialize(void);
  void Process(bool bPlugAndPlayUpdated);

  virtual void Run(unsigned nCore) override;
  

  static void USBMIDIMessageHandler(unsigned nCable, u8 *pPacket,
                                    unsigned nLength);
  static void DeviceRemovedHandler(CDevice *pDevice, void *pContext);
  
  MidiParser midiParser;
    CSerialDevice& GetSerial() { return m_Serial; }
     uint8_t* GetMIDIBuffer() { return m_MIDIBuffer; }

    // MIDI
    void HandleFullMIDIMessage(const uint8_t* data, uint8_t length);
  //static void ParseMIDIData(CMiniJV880* pThis, const u8* pData, unsigned nLength);
  void UnscrambleRom(const uint8_t *src, uint8_t *dst, int len);
  bool FileExists(const char* filename);
  bool LoadRom(uint8_t rom_index);
  bool LoadMainRoms(uint8_t ExpRom);
  bool LoadFile(const char* filename, uint8_t* data, size_t size);
  void LogMCU(uint64_t logcyc,uint64_t  logwriteptr,int  logsleep,int  logex);
  void LogPCM(uint64_t  logcyc1);
  int s_log_counter = 0;
  void SaveNVRAMIncremental();
  void switchPatchBank(int bankNumber);
  void InitBankMappings();
  void ParseAndAddMapping(const char* filename);

  // Launch Control XL state used by the extended display.
  bool IsPatchMode() const;
  unsigned GetMIDISurfaceBank() const;
  const char *GetMIDISurfaceRowLabel(unsigned row) const;
  void FormatMIDISurfaceToneValue(unsigned row, unsigned tone,
                                  char *text, unsigned textSize) const;

  bool InitNetwork();
	void UpdateNetwork();


  MCU mcu;

private:

  struct RomInfo {
        size_t size;
        const char* filename;
        bool isWaveRom;
        bool isLoaded;
        bool needsUnscramble;
        void* data;
    };

  struct BankMapping {
      int bankNumber;
      int romIndex;
      char nvramFilename[32];  // Store nvram filename for loading
  };

  BankMapping* m_bankMappings;
  unsigned m_bankMappingsCount;
  unsigned m_bankMappingsCapacity;
  int m_currentExpansionRomIndex;
  int m_currentBankNumber;

  static constexpr size_t sz32K = 32 * 1024;
  static constexpr size_t sz128K = 128 * 1024;
  static constexpr size_t sz256K = 256 * 1024;
  static constexpr size_t sz2M = 2 * 1024 * 1024;
  static constexpr size_t sz8M = 8 * 1024 * 1024;

  static RomInfo m_romInfos[27];
  static constexpr size_t ROM_COUNT = 27;

  CConfig *m_pConfig;
  FATFS *m_pFileSystem;

  CUSBMIDIDevice *volatile m_pMIDIDevice = 0;
  CSerialDevice m_Serial;
  uint8_t m_MIDIBuffer[256];
  uint8_t m_nBankMSB[16] = {0};
  
  enum SurfaceToneParameter {
    SurfaceToneSwitch = 0,
    SurfaceToneLevel,
    SurfaceTonePan,
    SurfaceToneReverb,
    SurfaceToneChorus,
    SurfaceToneAttack,
    SurfaceToneDecay,
    SurfaceToneResonance,
    SurfaceToneCutoff,
    SurfaceToneParameterCount
  };

  enum SurfaceCommonParameter {
    SurfaceCommonLevel = 0,
    SurfaceCommonPan,
    SurfaceCommonReverb,
    SurfaceCommonChorus,
    SurfaceCommonParameterCount
  };

  struct SurfacePickupState {
    bool latched;
    bool hasPrevious;
    uint8_t previous;
  };

  void ProcessMIDISurface();
  bool MIDISurfaceChannelMatches(uint8_t channel) const;
  bool HandleMIDISurfaceCC(uint8_t channel, uint8_t cc, uint8_t value);
  bool HandleMIDISurfaceButton(uint8_t channel, uint8_t number, bool pressed);
  bool GetMIDISurfaceTarget(unsigned row, unsigned column, unsigned &target) const;
  bool ApplyMIDISurfacePickup(unsigned control, unsigned target, uint8_t value);
  void ResetMIDISurfacePickup(bool toneControlsOnly = false);

  void SendRolandRQ1(const uint8_t address[4], const uint8_t size[4]);
  void SendRolandDT1(const uint8_t address[4], const uint8_t *data, unsigned length);
  void DrainEmulatedMIDIOut();
  void ParseEmulatedSysEx(const uint8_t *message, unsigned length);
  void UpdateSurfaceCacheFromDT1(const uint8_t startAddress[4],
                                 const uint8_t *data, unsigned length);
  void PollMIDISurface();
  void InvalidateMIDISurfaceState();

  void SetToneParameter(unsigned tone, SurfaceToneParameter parameter, uint8_t value);
  void SetCommonParameter(SurfaceCommonParameter parameter, uint8_t value);
  void SetGlobalToneMacro(SurfaceToneParameter parameter, uint8_t value);
  void SetPerformanceParameter(unsigned part, unsigned offset, uint8_t value);
  void SelectSurfaceTone(unsigned tone);
  void QueuePatchBank(int bankNumber);
  void SelectAdjacentExpansion(int direction);
  void SelectAdjacentBank(int direction);

  void MarkMIDISurfaceLEDsDirty();
  void UpdateMIDISurfaceLEDs();
  bool SendLaunchControlLED(unsigned button, uint8_t colour);

  bool m_bMIDISurfaceEnabled = false;
  unsigned m_nMIDISurfaceChannel = 0;
  unsigned m_nMIDISurfaceDeviceID = 16;
  bool m_bMIDISurfaceLEDFeedback = true;
  unsigned m_nMIDISurfaceTemplate = 0;
  unsigned m_nMIDISurfaceCCStart[4] = {24, 32, 40, 16};
  unsigned m_nMIDISurfaceButtons[16] = {0};
  bool m_bMIDISurfacePickupEnabled = true;
  unsigned m_nMIDISurfacePickupRange = 2;
  std::atomic<unsigned> m_nMIDISurfaceBank{1};
  unsigned m_nMIDISurfaceSelectedTone = 0;
  bool m_bMIDISurfaceLastPatchMode = false;
  SurfacePickupState m_MIDISurfacePickup[32];

  std::atomic<unsigned> m_SurfaceToneValues[4][SurfaceToneParameterCount];
  std::atomic<unsigned> m_SurfaceToneValidMask[4];
  std::atomic<unsigned> m_SurfaceCommonValues[SurfaceCommonParameterCount];
  std::atomic<unsigned> m_SurfaceCommonValidMask{0};
  std::atomic<unsigned> m_SurfacePartEnabled[8];
  std::atomic<unsigned> m_SurfacePartLevel[8];
  std::atomic<unsigned> m_SurfacePartPan[8];
  std::atomic<unsigned> m_SurfacePartFieldMask[8];

  uint8_t m_SurfacePatchName[12] = {0};
  bool m_bSurfacePatchNameValid = false;
  uint8_t m_SurfaceTXSysEx[512] = {0};
  unsigned m_nSurfaceTXSysExLength = 0;
  bool m_bSurfaceTXInSysEx = false;
  unsigned m_nSurfacePollPhase = 0;
  unsigned m_nSurfaceLastPoll = 0;
  bool m_bMIDISurfaceLEDDirty = true;
  uint8_t m_nMIDISurfaceLastLED[16];
  unsigned m_nMIDISurfaceLastLEDUpdate = 0;

  int lastEncoderPos = 0;

  CSoundBaseDevice *m_pSoundDevice;
  CScreenDevice *screenUnbuffered;
  bool m_bChannelsSwapped;
  unsigned m_nQueueSizeFrames;
  CUserInterface m_UI;
  
	// Network
	CNetSubSystem* m_pNet;
	CNetDevice* m_pNetDevice;
	CBcm4343Device* m_WLAN; 
	CWPASupplicant* m_WPASupplicant; 
	bool m_bNetworkReady;
	bool m_bNetworkInit;
	CUDPMIDIDevice* m_UDPMIDI; 
	CFTPDaemon* m_pFTPDaemon;
	CmDNSPublisher *m_pmDNSPublisher;

  unsigned m_lastTick;
  unsigned m_lastTick1;

  static CMiniJV880 *s_pThis;
  unsigned n_mMCUcycles = 9;
  int m_nNVRAMSaveCounter = 0;
  std::atomic<int> m_nPendingBankSwitch{-1};
  std::atomic<uint32_t> m_nBankSwitchTimestamp;
  static constexpr uint32_t BANK_SWITCH_DEBOUNCE_US = 300000; //us 
  std::atomic<bool> m_bAudioPaused{false};
  


    
};

#endif
