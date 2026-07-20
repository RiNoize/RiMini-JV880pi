//
// userinterface.cpp
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
#include "userinterface.h"
#include "version.h"
#include "minijv880.h"
#include "emulator/mcu.h"
#include "drivers/font5x8.h"
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/startup.h>
#include <circle/timer.h>
#include <string.h>
#include <assert.h>
#include <chrono>

LOGMODULE ("ui");

bool CUserInterface::g_ServiceActive = false;
unsigned long CUserInterface::g_ServiceStart = 0;
CString CUserInterface::g_ServiceLine[2];

CUserInterface::CUserInterface (CMiniJV880 *pMiniJV880, CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster, CConfig *pConfig, CScreenDevice *pHDMIDisplay, CWriteBufferDevice *pHDMIScreen)
:	m_pMiniJV880 (pMiniJV880),
	m_pGPIOManager (pGPIOManager),
	m_pI2CMaster (pI2CMaster),
	m_pSPIMaster (pSPIMaster),
	m_pConfig (pConfig),
	m_pLCD (0),
	m_pLCDBuffered (0),
	m_pHDMIDisplay (pHDMIDisplay),
	m_pHDMIScreen (pHDMIScreen),
	m_pUIButtons (0),
	m_pRotaryEncoder (0),
	m_bSwitchPressed (false),
	m_lastTick (0),
	m_lastHDMIUpdate (0),
	m_bHDMIFirstFrame (true),
	m_lastHDMIScale (0),
	m_lastHDMIX ((unsigned) -1),
	m_lastHDMIY ((unsigned) -1)
{
	screen_buffer = (u8 *)malloc(512);
	memset (m_lastHDMIPanel, 0, sizeof m_lastHDMIPanel);
}

CUserInterface::~CUserInterface (void)
{
	delete m_pRotaryEncoder;
	delete m_pUIButtons;
	delete m_pLCDBuffered;
	delete m_pLCD;
}

bool CUserInterface::Initialize (void)
{
	assert (m_pConfig);

	// Cache MIDI button configuration
    m_nMIDIButtonChannel = 	m_pConfig->GetMIDIButtonCh();
    m_nMIDIPreview = 		m_pConfig->GetMIDIButtonPreview() & 0x7F;
    m_nMIDILeft = 			m_pConfig->GetMIDIButtonLeft() & 0x7F;
    m_nMIDIRight = 			m_pConfig->GetMIDIButtonRight() & 0x7F;
    m_nMIDIData = 			m_pConfig->GetMIDIButtonData() & 0x7F;
    m_nMIDIToneSelect = 	m_pConfig->GetMIDIButtonToneSelect() & 0x7F;
    m_nMIDIPatchPerform = 	m_pConfig->GetMIDIButtonPatchPerform() & 0x7F;
    m_nMIDIEdit = 			m_pConfig->GetMIDIButtonEdit() & 0x7F;
    m_nMIDISystem = 		m_pConfig->GetMIDIButtonSystem() & 0x7F;
    m_nMIDIRhythm = 		m_pConfig->GetMIDIButtonRhythm() & 0x7F;
    m_nMIDIUtility = 		m_pConfig->GetMIDIButtonUtility() & 0x7F;
    m_nMIDIMute = 			m_pConfig->GetMIDIButtonMute() & 0x7F;
    m_nMIDIMonitor = 		m_pConfig->GetMIDIButtonMonitor() & 0x7F;
    m_nMIDICompare = 		m_pConfig->GetMIDIButtonCompare() & 0x7F;
    m_nMIDIEnter = 			m_pConfig->GetMIDIButtonEnter() & 0x7F; 
	m_nMIDIUp = 			m_pConfig->GetMIDIButtonUp() & 0x7F; 
	m_nMIDIDown = 			m_pConfig->GetMIDIButtonDown() & 0x7F; 
	m_nMIDISaveNVRAM = 		m_pConfig->GetMIDISaveNVRAM() & 0x7F; 
	m_nMIDIEncoder = 		m_pConfig->GetMIDIEncoder() & 0x7F; 
	m_nMIDIEncoderCC = 		m_pConfig->GetMIDIEncoderCC() & 0x7F; 
	m_nMIDIEncoderUp = 		m_pConfig->GetMIDIEncoderUp() & 0x7F; 
	m_nMIDIEncoderDown = 	m_pConfig->GetMIDIEncoderDown() & 0x7F; 

	
	if (!LCDInit()) {
		LOGNOTE("Display init error");
		return false;
	}

	m_pUIButtons = new CUIButtons (
                  m_pConfig->GetButtonPinPreview (), m_pConfig->GetButtonActionPreview (),
									m_pConfig->GetButtonPinLeft (), m_pConfig->GetButtonActionRight (),
									m_pConfig->GetButtonPinRight (), m_pConfig->GetButtonActionRight (),
									m_pConfig->GetButtonPinData (), m_pConfig->GetButtonActionData (),
									m_pConfig->GetButtonPinToneSelect (), m_pConfig->GetButtonActionToneSelect (),
                  m_pConfig->GetButtonPinPatchPerform (), m_pConfig->GetButtonActionPatchPerform (),
									m_pConfig->GetButtonPinEdit (), m_pConfig->GetButtonActionEdit (),
									m_pConfig->GetButtonPinSystem (), m_pConfig->GetButtonActionSystem (),
									m_pConfig->GetButtonPinRhythm (), m_pConfig->GetButtonActionRhythm (),
									m_pConfig->GetButtonPinUtility (), m_pConfig->GetButtonActionUtility (),
                  m_pConfig->GetButtonPinMute (), m_pConfig->GetButtonActionMute (),
                  m_pConfig->GetButtonPinMonitor (), m_pConfig->GetButtonActionMonitor (),
                  m_pConfig->GetButtonPinCompare (), m_pConfig->GetButtonActionCompare (),
									m_pConfig->GetButtonPinEnter (), m_pConfig->GetButtonActionEnter (),
									m_pConfig->GetButtonPinUp (), m_pConfig->GetButtonActionUp (),
									m_pConfig->GetButtonPinDown (), m_pConfig->GetButtonActionDown (),
                                    m_pConfig->GetButtonPinSaveNVRAM (), m_pConfig->GetButtonActionSaveNVRAM (),
									m_pConfig->GetDoubleClickTimeout (), m_pConfig->GetLongPressTimeout ()
								  );
	assert (m_pUIButtons);

	if (!m_pUIButtons->Initialize ())
	{
		return false;
	}

	m_pUIButtons->RegisterEventHandler (UIButtonsEventStub, this);

	LOGDBG ("Button User Interface initialized");

	if (m_pConfig->GetEncoderEnabled ())
	{
		m_pRotaryEncoder = new CKY040 (m_pConfig->GetEncoderPinClock (),
					       m_pConfig->GetEncoderPinData (),
					       m_pConfig->GetButtonPinEnter (),
					       m_pGPIOManager);
		assert (m_pRotaryEncoder);

		if (!m_pRotaryEncoder->Initialize ())
		{
			return false;
		}

		m_pRotaryEncoder->RegisterEventHandler (EncoderEventStub, this);

		LOGDBG ("Rotary encoder initialized");
	}
   
	return true;
}

void CUserInterface::Process(void)
{
    m_pMiniJV880->mcu.lcd.LCD_Update();
    if (m_pUIButtons) m_pUIButtons->Update();

    RenderDisplay(); 

    if (m_pLCDBuffered) m_pLCDBuffered->Update(256);

}

bool CUserInterface::LCDInit()
{
	if (m_pConfig->GetLCDEnabled ())
	{
		unsigned i2caddr = m_pConfig->GetLCDI2CAddress ();
		unsigned ssd1306addr = m_pConfig->GetSSD1306LCDI2CAddress ();
		unsigned sh1106addr = m_pConfig->GetSH1106LCDI2CAddress ();
		bool st7789 = m_pConfig->GetST7789Enabled ();
		unsigned lcdColumns = m_pConfig->GetLCDColumns(); // Get number of columns
		
		if (sh1106addr != 0) {
			CSH1106Device *pSH1106 = new CSH1106Device (
				m_pConfig->GetLCDColumns (),
				m_pConfig->GetLCDRows (),
				m_pI2CMaster,
				sh1106addr,
				m_pConfig->GetSH1106LCDRotate (),
				m_pConfig->GetSH1106LCDMirror ()
			);

			if (!pSH1106->Initialize ())
			{
				LOGDBG ("LCD: SH1106 initialization failed");
				delete pSH1106;
				return false;
			}

			LOGDBG ("LCD: SH1106 (128x64 I2C, %ux%u characters)",
				m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows ());
			m_pLCD = pSH1106;
		}
		else if (ssd1306addr != 0) {
			if (lcdColumns >= 24) {
				// Use 24-driver (5x8 font) for 24 columns
				CSSD1306Device24* pSSD1306Device24 = new CSSD1306Device24 (
					m_pConfig->GetSSD1306LCDWidth (), 
					m_pConfig->GetSSD1306LCDHeight (),
					m_pI2CMaster, 
					ssd1306addr,
					m_pConfig->GetSSD1306LCDRotate (), 
					m_pConfig->GetSSD1306LCDMirror ()
				);
				
				if (!pSSD1306Device24->Initialize24 ())
				{
					LOGDBG("LCD: SSD1306 24 initialization failed");
					delete pSSD1306Device24;
					return false;
				}
				
				LOGDBG ("LCD: SSD1306 24 (5x8 font, 24 columns)");
				m_pLCD = pSSD1306Device24; // Assign directly to m_pLCD
				// m_pSSD1306 remains NULL
			} else {
				// Use original driver (6x8 font) for 20 columns
				m_pSSD1306 = new CSSD1306Device (
					m_pConfig->GetSSD1306LCDWidth (), 
					m_pConfig->GetSSD1306LCDHeight (),
					m_pI2CMaster, 
					ssd1306addr,
					m_pConfig->GetSSD1306LCDRotate (), 
					m_pConfig->GetSSD1306LCDMirror ()
				);
				
				if (!m_pSSD1306->Initialize ())
				{
					LOGDBG("LCD: SSD1306 initialization failed");
					return false;
				}
				
				LOGDBG ("LCD: SSD1306 (6x8 font, 20 columns)");
				m_pLCD = m_pSSD1306;
			}
		}
		else if (st7789)
		{
			if (m_pSPIMaster == nullptr)
			{
				LOGDBG("LCD: ST7789 Enabled but SPI Initialisation Failed");
				return false;
			}

			unsigned long nSPIClock = 1000 * m_pConfig->GetSPIClockKHz();
			unsigned nSPIMode = m_pConfig->GetSPIMode();
			unsigned nCPHA = (nSPIMode & 1) ? 1 : 0;
			unsigned nCPOL = (nSPIMode & 2) ? 1 : 0;
			LOGDBG("SPI: CPOL=%u; CPHA=%u; CLK=%u",nCPOL,nCPHA,nSPIClock);
			m_pST7789Display = new CST7789Display (m_pSPIMaster,
							m_pConfig->GetST7789Data(),
							m_pConfig->GetST7789Reset(),
							m_pConfig->GetST7789Backlight(),
							m_pConfig->GetST7789Width(),
							m_pConfig->GetST7789Height(),
							nCPOL, nCPHA, nSPIClock,
							m_pConfig->GetST7789Select());
			if (m_pST7789Display->Initialize())
			{
				m_pST7789Display->SetRotation (m_pConfig->GetST7789Rotation());
				//bool bLargeFont = !(m_pConfig->GetST7789SmallFont());
				m_pST7789 = new CST7789Device (m_pSPIMaster, m_pST7789Display, m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows (), Font8x16, false, false);
				if (m_pST7789->Initialize())
				{
					LOGDBG ("LCD: ST7789");
					m_pLCD = m_pST7789;
				}
				else
				{
					LOGDBG ("LCD: Failed to initalize ST7789 character device");
					delete (m_pST7789);
					delete (m_pST7789Display);
					m_pST7789 = nullptr;
					m_pST7789Display = nullptr;
					return false;
				}
			}
			else
			{
				LOGDBG ("LCD: Failed to initialize ST7789 display");
				delete (m_pST7789Display);
				m_pST7789Display = nullptr;
				return false;
			}
		}
		else if (i2caddr == 0)
		{
			m_pHD44780 = new CHD44780Device (m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows (),
							 m_pConfig->GetLCDPinData4 (),
							 m_pConfig->GetLCDPinData5 (),
							 m_pConfig->GetLCDPinData6 (),
							 m_pConfig->GetLCDPinData7 (),
							 m_pConfig->GetLCDPinEnable (),
							 m_pConfig->GetLCDPinRegisterSelect (),
							 m_pConfig->GetLCDPinReadWrite ());
			if (!m_pHD44780->Initialize ())
			{
				LOGDBG("LCD: HD44780 initialization failed");
				return false;
			}
			LOGDBG ("LCD: HD44780");
			m_pLCD = m_pHD44780;
		}
		else
		{
			m_pHD44780 = new CHD44780Device (m_pI2CMaster, i2caddr,
							m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows ());
			if (!m_pHD44780->Initialize ())
			{
				LOGDBG("LCD: HD44780 (I2C) initialization failed");
				return false;
			}
			LOGDBG ("LCD: HD44780 I2C");
			m_pLCD = m_pHD44780;
		}
		if (!m_pLCD) {
			LOGDBG("LCD: No display device initialized");
			return false;
		}

		m_pLCDBuffered = new CWriteBufferDevice (m_pLCD, 256);
		assert (m_pLCDBuffered);

		LCDWrite ("\x1B[?25l\x1B""d+");		// cursor off, autopage mode
		LCDWrite ("Start Mini-JV880pi\n");
		LCDWrite ("version ");
		LCDWrite (VERSION_SHORT);
		m_pLCDBuffered->Update ();

		LOGDBG ("LCD initialized");
	}	
	return true;
}

void CUserInterface::LCDWrite (const char *pString)
{
	if (m_pLCDBuffered)
	{
		m_pLCDBuffered->Write (pString, strlen (pString));
	}
}

void CUserInterface::EncoderEventHandler (CKY040::TEvent Event)
{
	//uint32_t btn = 0;
	switch (Event)
	{
	case CKY040::EventSwitchDown:
		m_bSwitchPressed = true;
		break;

	case CKY040::EventSwitchUp:
		m_bSwitchPressed = false;
		break;

	case CKY040::EventClockwise:
		if (m_bSwitchPressed) {
			// We must reset the encoder switch button to prevent events from being
			// triggered after the encoder is rotated
			m_pUIButtons->ResetButton(m_pConfig->GetButtonPinEnter());
		} else {
      m_pMiniJV880->mcu.MCU_EncoderTrigger(1);
    }
		break;

	case CKY040::EventCounterclockwise:
		if (m_bSwitchPressed) {
			m_pUIButtons->ResetButton(m_pConfig->GetButtonPinEnter());
		} else {
			m_pMiniJV880->mcu.MCU_EncoderTrigger(0);
		}
		break;

	case CKY040::EventSwitchHold:
		if (m_pRotaryEncoder->GetHoldSeconds () >= 120)
		{
			delete m_pLCD;		// reset LCD

			reboot ();
		}
		break;

	default:
		break;
	}
}

void CUserInterface::EncoderEventStub (CKY040::TEvent Event, void *pParam)
{
	CUserInterface *pThis = static_cast<CUserInterface *> (pParam);
	assert (pThis != 0);

	pThis->EncoderEventHandler (Event);
}

void CUserInterface::UIButtonsEventHandler (CUIButton::BtnEvent Event)
{
	uint32_t btn = 0;
	if (Event == CUIButton::BtnEventRelease) {
        btn = 0; 
    }
	if (Event == CUIButton::BtnEventPreview) {
		btn |= 1 << MCU_BUTTON_PREVIEW;
	} else {
		btn &= ~(1 << MCU_BUTTON_PREVIEW);
	}
	if (Event == CUIButton::BtnEventLeft) {
		btn |= 1 << MCU_BUTTON_CURSOR_L;
	} else {
		btn &= ~(1 << MCU_BUTTON_CURSOR_L);
	}
	if (Event == CUIButton::BtnEventRight) {
		btn |= 1 << MCU_BUTTON_CURSOR_R;
	} else {
		btn &= ~(1 << MCU_BUTTON_CURSOR_R);
	}
	if (Event == CUIButton::BtnEventData) {
		btn |= 1 << MCU_BUTTON_DATA;
	} else {
		btn &= ~(1 << MCU_BUTTON_DATA);
	}
	if (Event == CUIButton::BtnEventToneSelect) {
		btn |= 1 << MCU_BUTTON_TONE_SELECT;
	} else {
		btn &= ~(1 << MCU_BUTTON_TONE_SELECT);
	}
	if (Event == CUIButton::BtnEventPatchPerform) {
		btn |= 1 << MCU_BUTTON_PATCH_PERFORM;
	} else {
		btn &= ~(1 << MCU_BUTTON_PATCH_PERFORM);
	}
	if (Event == CUIButton::BtnEventEdit) {
		btn |= 1 << MCU_BUTTON_EDIT;
	} else {
		btn &= ~(1 << MCU_BUTTON_EDIT);
	}
	if (Event == CUIButton::BtnEventSystem) {
		btn |= 1 << MCU_BUTTON_SYSTEM;
	} else {
		btn &= ~(1 << MCU_BUTTON_SYSTEM);
	}
	if (Event == CUIButton::BtnEventRhythm) {
		btn |= 1 << MCU_BUTTON_RHYTHM;
	} else {
		btn &= ~(1 << MCU_BUTTON_RHYTHM);
	}
	if (Event == CUIButton::BtnEventUtility) {
		btn |= 1 << MCU_BUTTON_UTILITY;
	} else {
		btn &= ~(1 << MCU_BUTTON_UTILITY);
	}
	if (Event == CUIButton::BtnEventMute) {
			btn |= 1 << MCU_BUTTON_MUTE;
	} else {
		btn &= ~(1 << MCU_BUTTON_MUTE);
	}
	if (Event == CUIButton::BtnEventMonitor) {
		btn |= 1 << MCU_BUTTON_MONITOR;
	} else {
		btn &= ~(1 << MCU_BUTTON_MONITOR);
	}
	if (Event == CUIButton::BtnEventCompare) {
		btn |= 1 << MCU_BUTTON_COMPARE;
	} else {
		btn &= ~(1 << MCU_BUTTON_COMPARE);
	}
	if (Event == CUIButton::BtnEventEnter) {
		btn |= 1 << MCU_BUTTON_ENTER;
	} else {
		btn &= ~(1 << MCU_BUTTON_ENTER);
	}
	if (Event == CUIButton::BtnEventUp) {
		m_pMiniJV880->mcu.MCU_EncoderTrigger(1);
	} 
	if (Event == CUIButton::BtnEventDown) {
		m_pMiniJV880->mcu.MCU_EncoderTrigger(0);
	}
    if (Event == CUIButton::BtnEventSaveNVRAM) {
		m_pMiniJV880->SaveNVRAMIncremental();
	} 

	//LOGNOTE("Button: %x", btn);
	m_pMiniJV880->mcu.mcu_button_pressed = btn;
	//LOGNOTE("Led state: 0x%08X", m_pMiniJV880->mcu.jv880_led_state);
}

void CUserInterface::UIButtonsEventStub (CUIButton::BtnEvent Event, void *pParam)
{
	CUserInterface *pThis = static_cast<CUserInterface *> (pParam);
	assert (pThis != 0);

	pThis->UIButtonsEventHandler (Event);
}

void CUserInterface::TriggerUIButtonEvent(CUIButton::BtnEvent event)
{
    if (event != CUIButton::BtnEventNone)
    {
        UIButtonsEventHandler(event);
    }
}

void CUserInterface::LCDMessage(const char* fmt, ...)
{
    char buf[128]; 
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Find newline
    char* nl = strchr(buf, '\n');
    if (nl)
    {
        *nl = '\0'; // Split at newline
        g_ServiceLine[0].Format("%s", buf);      
        g_ServiceLine[1].Format("%s", nl + 1);   
    }
    else
    {
        g_ServiceLine[0].Format("%s", buf);
        g_ServiceLine[1] = "";
    }

    g_ServiceActive = true;
    g_ServiceStart = CTimer::GetClockTicks();
}



namespace
{
	static const unsigned HDMI_CONTENT_COLS = 25;
	static const unsigned HDMI_CONTENT_ROWS = 4;
	static const unsigned HDMI_PANEL_COLS = 27;
	static const unsigned HDMI_PANEL_ROWS = 7;
	static const unsigned HDMI_CELL_WIDTH = FONT5X8_WIDTH + 1;
	static const unsigned HDMI_CELL_HEIGHT = FONT5X8_HEIGHT + 1;

	static void DrawHDMICell (CScreenDevice *pScreen, unsigned x, unsigned y,
							 char ch, unsigned scale)
	{
		if (!pScreen || scale == 0)
			return;

		const unsigned screenWidth = pScreen->GetWidth ();
		const unsigned screenHeight = pScreen->GetHeight ();
		const unsigned cellWidth = HDMI_CELL_WIDTH * scale;
		const unsigned cellHeight = HDMI_CELL_HEIGHT * scale;

		// Paint the complete cell black first. This removes the previous glyph.
		for (unsigned py = 0; py < cellHeight; ++py)
		{
			const unsigned sy = y + py;
			if (sy >= screenHeight)
				break;

			for (unsigned px = 0; px < cellWidth; ++px)
			{
				const unsigned sx = x + px;
				if (sx >= screenWidth)
					break;
				pScreen->SetPixel (sx, sy, BLACK_COLOR);
			}
		}

		unsigned glyphIndex = 0;
		const unsigned uch = (unsigned char) ch;
		if (uch >= 32 && uch < 32 + FONT5X8_SIZE)
			glyphIndex = uch - 32;

		for (unsigned gy = 0; gy < FONT5X8_HEIGHT; ++gy)
		{
			const u8 rowBits = Font5x8[glyphIndex][gy];
			for (unsigned gx = 0; gx < FONT5X8_WIDTH; ++gx)
			{
				if ((rowBits & (1u << (FONT5X8_WIDTH - 1 - gx))) == 0)
					continue;

				const unsigned pixelX = x + gx * scale;
				const unsigned pixelY = y + gy * scale;
				for (unsigned sy = 0; sy < scale; ++sy)
				{
					if (pixelY + sy >= screenHeight)
						break;
					for (unsigned sx = 0; sx < scale; ++sx)
					{
						if (pixelX + sx >= screenWidth)
							break;
						pScreen->SetPixel (pixelX + sx, pixelY + sy, WHITE_COLOR);
					}
				}
			}
		}
	}
}

void CUserInterface::RenderHDMIDisplay(unsigned long currentTime, bool emuActive, bool showService)
{
	if (!m_pConfig->GetHDMIDisplayEnabled () || !m_pHDMIDisplay || !m_pHDMIScreen)
		return;

	// The main loop can run very quickly. 20 checks per second is enough for
	// the front panel. Pixels are redrawn only when the contents actually change.
	if (m_lastHDMIUpdate != 0 && currentTime - m_lastHDMIUpdate < 50000)
		return;
	m_lastHDMIUpdate = currentTime;

	char content[HDMI_CONTENT_ROWS][HDMI_CONTENT_COLS + 1];
	for (unsigned row = 0; row < HDMI_CONTENT_ROWS; ++row)
	{
		memset (content[row], ' ', HDMI_CONTENT_COLS);
		content[row][HDMI_CONTENT_COLS] = 0;
	}

	// Top two rows: original JV-880 LCD data.
	if (emuActive)
	{
		const int cursorRow = m_pMiniJV880->mcu.lcd.LCD_DD_RAM / 0x40;
		const int cursorCol = m_pMiniJV880->mcu.lcd.LCD_DD_RAM % 0x40;
		const bool cursorEnabled = m_pMiniJV880->mcu.lcd.LCD_C != 0;

		for (unsigned row = 0; row < 2; ++row)
		{
			for (unsigned col = 0; col < HDMI_CONTENT_COLS; ++col)
			{
				u8 ch = (col < ACTUAL_COLS)
					? m_pMiniJV880->mcu.lcd.LCD_Data[row * 40 + col]
					: ' ';

				if (ch == 0x09) ch = '|';
				else if (ch < 32 || ch > 126) ch = ' ';

				content[row][col] = (cursorEnabled
					&& (int) row == cursorRow && (int) col == cursorCol)
					? '_'
					: (char) ch;
			}
		}
	}
	else
	{
		const char *line1 = "Start Mini-JV880pi";
		char line2[64];
		snprintf (line2, sizeof line2, "version %s", VERSION_SHORT);
		memcpy (content[0], line1,
			strlen (line1) < HDMI_CONTENT_COLS ? strlen (line1) : HDMI_CONTENT_COLS);
		memcpy (content[1], line2,
			strlen (line2) < HDMI_CONTENT_COLS ? strlen (line2) : HDMI_CONTENT_COLS);
	}

	// Bottom two rows: service messages or front-panel LED labels.
	if (showService)
	{
		for (unsigned row = 0; row < 2; ++row)
		{
			const unsigned length = g_ServiceLine[row].GetLength ();
			for (unsigned col = 0; col < HDMI_CONTENT_COLS && col < length; ++col)
			{
				const char ch = g_ServiceLine[row][col];
				content[row + 2][col] = (ch >= 32 && ch <= 126) ? ch : ' ';
			}
		}
	}
	else if (emuActive)
	{
		const u16 ledState = m_pMiniJV880->mcu.jv880_led_state;
		const char *ledNames[] = {
			"MIDI", "Edit", "Syst", "Ryth", "Util",
			"PPrf", "Mute", "Moni", "Info", "Entr"
		};
		const char *toneNames[] = { "Ton1", "Ton2", "Ton3", "Ton4" };
		const bool patchMode = (ledState & (1 << 5)) != 0;

		for (unsigned index = 0; index < 10; ++index)
		{
			const bool isOn = (ledState & (1 << index)) != 0;
			const char *name = 0;

			if (index == 5)
				name = isOn ? "Ptch" : "Perf";
			else if (index >= 6 && index <= 9 && isOn)
				name = patchMode ? toneNames[index - 6] : ledNames[index];
			else if (isOn)
				name = ledNames[index];

			if (name)
			{
				const unsigned row = 2 + index / 5;
				const unsigned col = (index % 5) * 5;
				memcpy (&content[row][col], name, 4);
			}
		}
	}

	char panel[HDMI_PANEL_ROWS][HDMI_PANEL_COLS + 1];
	for (unsigned row = 0; row < HDMI_PANEL_ROWS; ++row)
	{
		memset (panel[row], ' ', HDMI_PANEL_COLS);
		panel[row][HDMI_PANEL_COLS] = 0;
	}

	char title[64];
	snprintf (title, sizeof title, "Mini-JV880pi %s", VERSION_SHORT);
	unsigned titleLength = strlen (title);
	if (titleLength > HDMI_PANEL_COLS)
		titleLength = HDMI_PANEL_COLS;
	const unsigned titleStart = (HDMI_PANEL_COLS - titleLength) / 2;
	memcpy (&panel[0][titleStart], title, titleLength);

	panel[1][0] = '+';
	panel[1][HDMI_PANEL_COLS - 1] = '+';
	panel[HDMI_PANEL_ROWS - 1][0] = '+';
	panel[HDMI_PANEL_ROWS - 1][HDMI_PANEL_COLS - 1] = '+';
	for (unsigned col = 1; col < HDMI_PANEL_COLS - 1; ++col)
	{
		panel[1][col] = '-';
		panel[HDMI_PANEL_ROWS - 1][col] = '-';
	}
	for (unsigned row = 0; row < HDMI_CONTENT_ROWS; ++row)
	{
		panel[row + 2][0] = '|';
		panel[row + 2][HDMI_PANEL_COLS - 1] = '|';
		memcpy (&panel[row + 2][1], content[row], HDMI_CONTENT_COLS);
	}

	const unsigned screenWidth = m_pHDMIDisplay->GetWidth ();
	const unsigned screenHeight = m_pHDMIDisplay->GetHeight ();
	const unsigned terminalRows = m_pHDMIDisplay->GetRows ();
	const unsigned terminalCharHeight = terminalRows != 0
		? screenHeight / terminalRows
		: 16;
	unsigned margin = m_pConfig->GetHDMIDisplayMargin ();
	if (margin > 64) margin = 64;

	unsigned logRows = m_pConfig->GetHDMILogRows ();
	if (logRows == 0) logRows = 1;
	if (terminalRows > 1 && logRows >= terminalRows)
		logRows = terminalRows - 1;
	else if (terminalRows == 1)
		logRows = 1;

	const unsigned basePanelWidth = HDMI_PANEL_COLS * HDMI_CELL_WIDTH;
	const unsigned basePanelHeight = HDMI_PANEL_ROWS * HDMI_CELL_HEIGHT;

	// If the requested log area leaves no room, shrink the log area first.
	while (logRows > 1
		&& logRows * terminalCharHeight + basePanelHeight + 2 * margin > screenHeight)
	{
		--logRows;
	}

	const unsigned logPixelHeight = logRows * terminalCharHeight;
	const unsigned availableWidth = screenWidth > 2 * margin
		? screenWidth - 2 * margin
		: screenWidth;
	const unsigned availableHeight = screenHeight > logPixelHeight + 2 * margin
		? screenHeight - logPixelHeight - 2 * margin
		: basePanelHeight;

	unsigned fitScaleX = availableWidth / basePanelWidth;
	unsigned fitScaleY = availableHeight / basePanelHeight;
	unsigned fitScale = fitScaleX < fitScaleY ? fitScaleX : fitScaleY;
	if (fitScale < 1) fitScale = 1;
	if (fitScale > 8) fitScale = 8;

	unsigned scale = m_pConfig->GetHDMIDisplayScale ();
	if (scale == 0 || scale > fitScale)
		scale = fitScale;

	const unsigned panelWidth = basePanelWidth * scale;
	const unsigned panelHeight = basePanelHeight * scale;
	const unsigned panelX = screenWidth > panelWidth
		? (screenWidth - panelWidth) / 2
		: 0;
	const unsigned freeHeight = screenHeight > logPixelHeight + panelHeight
		? screenHeight - logPixelHeight - panelHeight
		: 0;
	const unsigned panelY = logPixelHeight + freeHeight / 2;

	if (m_bHDMIFirstFrame)
	{
		// Clear the old boot output, reserve only the upper rows for logs, and
		// return the terminal cursor to the log area. The panel is pixel-drawn
		// below this region, so later log lines cannot scroll through it.
		char terminalSetup[96];
		snprintf (terminalSetup, sizeof terminalSetup,
			"\x1B[2J\x1B[?25l\x1B[1;%ur\x1B[%u;1H", logRows, logRows);
		m_pHDMIScreen->Write (terminalSetup, strlen (terminalSetup));
		m_pHDMIScreen->Update ();
		m_bHDMIFirstFrame = false;
		memset (m_lastHDMIPanel, 0, sizeof m_lastHDMIPanel);
	}

	const bool geometryChanged = scale != m_lastHDMIScale
		|| panelX != m_lastHDMIX || panelY != m_lastHDMIY;
	const bool contentsChanged = memcmp (panel, m_lastHDMIPanel,
		sizeof m_lastHDMIPanel) != 0;

	if (!geometryChanged && !contentsChanged)
		return;

	for (unsigned row = 0; row < HDMI_PANEL_ROWS; ++row)
	{
		for (unsigned col = 0; col < HDMI_PANEL_COLS; ++col)
		{
			DrawHDMICell (m_pHDMIDisplay,
				panelX + col * HDMI_CELL_WIDTH * scale,
				panelY + row * HDMI_CELL_HEIGHT * scale,
				panel[row][col], scale);
		}
	}

	memcpy (m_lastHDMIPanel, panel, sizeof m_lastHDMIPanel);
	m_lastHDMIScale = scale;
	m_lastHDMIX = panelX;
	m_lastHDMIY = panelY;
}

void CUserInterface::RenderDisplay()
{
    // Clear screen and hide cursor
    CString Msg("\x1B[H\x1B[?25l");
    int displayCols = m_pConfig->GetLCDColumns();
    int displayRows = m_pConfig->GetLCDRows();
    unsigned long currentTime = CTimer::GetClockTicks();

    bool emuActive = (m_pMiniJV880->mcu.mcu.pc != 0);

    // Scroll constants
    const int SCROLL_INTERVAL = 500000; // 0.5 seconds in microseconds
    
    // Scroll state variables
    static int scrollPos = 0;
    static int scrollDir = 1;
    static unsigned long lastScrollTime = 0;

    // Calculate top and bottom rows
    int topRows = (displayRows >= 4) ? 2 : displayRows;
    int bottomRows = (displayRows >= 4) ? 2 : 0;

    // Determine service message visibility
    bool showService = false;
    if (g_ServiceActive)
    {
        if (currentTime - g_ServiceStart < 3000000) // 3 seconds
            showService = true;
        else
            g_ServiceActive = false;
    }

    RenderHDMIDisplay(currentTime, emuActive, showService);

    // 2-ROW MODE
    if (displayRows < 4)
    {
        if (showService)
        {
            // Service message
            for (int i = 0; i < displayRows; i++)
            {
                for (int c = 0; c < displayCols; c++)
                {
                    char ch = (c < (int)g_ServiceLine[i].GetLength()) ? g_ServiceLine[i][c] : ' ';
                    if (ch < 32 || ch > 126) ch = ' ';
                    char buf[2] = { ch, 0 };
                    Msg.Append(buf);
                }
            }
        }
        else if (emuActive)
        {
            // Emulator with scrolling
            int rowLen[2] = {0, 0};
            int maxRowLen = 0;
            for (int r = 0; r < displayRows; r++) {
                rowLen[r] = 0;
                for (int c = ACTUAL_COLS - 1; c >= 0; c--) {
                    if (m_pMiniJV880->mcu.lcd.LCD_Data[r * 40 + c] != ' ') {
                        rowLen[r] = c + 1;
                        break;
                    }
                }
                if (rowLen[r] > maxRowLen) {
                    maxRowLen = rowLen[r];
                }
            }

            // Scroll logic
            if (maxRowLen > displayCols) {
                if (currentTime - lastScrollTime >= SCROLL_INTERVAL) {
                    scrollPos += scrollDir;
                    if (scrollPos <= 0) {
                        scrollPos = 0;
                        scrollDir = +1;
                    } else if (scrollPos >= maxRowLen - displayCols) {
                        scrollPos = maxRowLen - displayCols;
                        scrollDir = -1;
                    }
                    lastScrollTime = currentTime;
                }
            } else {
                scrollPos = 0;
            }

            int cursorRow = m_pMiniJV880->mcu.lcd.LCD_DD_RAM / 0x40;
            int cursorCol = m_pMiniJV880->mcu.lcd.LCD_DD_RAM % 0x40;
            bool cursorEnabled = m_pMiniJV880->mcu.lcd.LCD_C != 0;
            if (cursorRow >= displayRows) cursorRow = 0;

            for (int row = 0; row < displayRows; row++)
            {
                int startPos = scrollPos;
                for (int col = 0; col < displayCols; col++)
                {
                    int sourcePos = col + startPos;
                    uint8_t ch = (sourcePos < ACTUAL_COLS) ? m_pMiniJV880->mcu.lcd.LCD_Data[row * 40 + sourcePos] : ' ';
                    if (ch == 0x09) ch = '|';
                    else if (ch < 32 || ch > 126) ch = ' ';

                    if (cursorEnabled && row == cursorRow && sourcePos == cursorCol)
                        Msg.Append("_");
                    else
                    {
                        char buf[2] = { (char)ch, 0 };
                        Msg.Append(buf);
                    }
                }
            }
        }
        else
        {
            // Start message
            const char* startMsg1 = "Start Mini-JV880pi";
            char startMsg2[64];
            snprintf(startMsg2, sizeof(startMsg2), "version %s", VERSION_SHORT);

            for (int i = 0; i < displayRows; i++)
            {
                const char* line = (i == 0) ? startMsg1 : startMsg2;
                int len = strlen(line);
                for (int c = 0; c < displayCols; c++)
                {
                    char ch = (c < len) ? line[c] : ' ';
                    char buf[2] = { ch, 0 };
                    Msg.Append(buf);
                }
            }
        }
    }
    // 4-ROW MODE
    else
    {
        // Render emulator or start message in top rows
        if (emuActive)
        {
            int cursorRow = m_pMiniJV880->mcu.lcd.LCD_DD_RAM / 0x40;
            int cursorCol = m_pMiniJV880->mcu.lcd.LCD_DD_RAM % 0x40;
            bool cursorEnabled = m_pMiniJV880->mcu.lcd.LCD_C != 0;
            if (cursorRow >= topRows) cursorRow = 0;

            for (int row = 0; row < topRows; row++)
            {
                int startPos = 0;
                for (int col = 0; col < displayCols; col++)
                {
                    int sourcePos = col + startPos;
                    uint8_t ch = (sourcePos < ACTUAL_COLS) ? m_pMiniJV880->mcu.lcd.LCD_Data[row * 40 + sourcePos] : ' ';
                    if (ch == 0x09) ch = '|';
                    else if (ch < 32 || ch > 126) ch = ' ';

                    if (cursorEnabled && row == cursorRow && sourcePos == cursorCol)
                        Msg.Append("_");
                    else
                    {
                        char buf[2] = { (char)ch, 0 };
                        Msg.Append(buf);
                    }
                }
            }
        }
        else
        {
            // Emulator not running - show start message in top 2 rows
            const char* startMsg1 = "Start Mini-JV880pi";
            char startMsg2[64];
            snprintf(startMsg2, sizeof(startMsg2), "version %s", VERSION_SHORT);

            for (int i = 0; i < topRows; i++)
            {
                const char* line = (i == 0) ? startMsg1 : startMsg2;
                for (int c = 0; c < displayCols; c++)
                {
                    char ch = (c < (int)strlen(line)) ? line[c] : ' ';
                    char buf[2] = { ch, 0 };
                    Msg.Append(buf);
                }
            }
        }

        // Render LED states or service message in bottom rows
        if (emuActive && !showService)
        {
            // Get LED state from MCU
            uint16_t ledState = m_pMiniJV880->mcu.jv880_led_state;
            
            // Define LED names (exactly 4 characters each + 1 space = 5 chars total)
            const char* ledNames[] = {
                "MIDI", "Edit", "Syst", "Ryth", "Util",
                "PPrf", "Mute", "Moni", "Info", "Entr"
            };

            // Render LED states - first 5 on first row, next 5 on second row
            // No cursor positioning - sequential output only
            for (int i = 0; i < 2; i++)
            {
                int ledsPerRow = 5;
                for (int j = 0; j < ledsPerRow; j++)
                {
                    int ledIndex = i * ledsPerRow + j;
                    bool isOn = (ledState & (1 << ledIndex)) != 0;
                    
                    // Special case for LED 5 (Part/Performance mode)
                    if (ledIndex == 5)
                    {
                        const char* name = isOn ? "Ptch" : "Perf";
                        for (int k = 0; k < 4; k++)
                        {
                            char buf[2] = { name[k], 0 };
                            Msg.Append(buf);
                        }
                        Msg.Append(" ");
                    }
                    // LED 6-9: In Pitch mode show Ton1-Ton4, otherwise show LED names
                    else if (ledIndex >= 6 && ledIndex <= 9)
                    {
                        bool pitchMode = (ledState & (1 << 5)) != 0;
                        
                        if (pitchMode && isOn)
                        {
                            // Show Tone numbers in Pitch mode when bit is ON
                            const char* toneNames[] = { "Ton1", "Ton2", "Ton3", "Ton4" };
                            const char* name = toneNames[ledIndex - 6];
                            
                            for (int k = 0; k < 4; k++)
                            {
                                char buf[2] = { name[k], 0 };
                                Msg.Append(buf);
                            }
                            Msg.Append(" ");
                        }
                        else if (!pitchMode && isOn)
                        {
                            // Show LED name in Performance mode when bit is ON
                            for (int k = 0; k < 4; k++)
                            {
                                char buf[2] = { ledNames[ledIndex][k], 0 };
                                Msg.Append(buf);
                            }
                            Msg.Append(" ");
                        }
                        else
                        {
                            // Display 5 spaces when bit is OFF
                            for (int k = 0; k < 5; k++)
                            {
                                Msg.Append(" ");
                            }
                        }
                    }
                    else if (isOn)
                    {
                        // Display the 4-character name + space when bit is active
                        for (int k = 0; k < 4; k++)
                        {
                            char buf[2] = { ledNames[ledIndex][k], 0 };
                            Msg.Append(buf);
                        }
                        Msg.Append(" ");
                    }
                    else
                    {
                        // Display 5 spaces when bit is inactive
                        for (int k = 0; k < 5; k++)
                        {
                            Msg.Append(" ");
                        }
                    }
                }
            }
        }
        else if (showService)
        {
            // Render service message in bottom rows
            for (int i = 0; i < bottomRows; i++)
            {
                for (int c = 0; c < displayCols; c++)
                {
                    char ch = (c < (int)g_ServiceLine[i].GetLength()) ? g_ServiceLine[i][c] : ' ';
                    char buf[2] = { ch, 0 };
                    Msg.Append(buf);
                }
            }
        }
        else
        {
            // Clear bottom rows when no service message and emulator inactive
            g_ServiceActive = false;
            for (int i = 0; i < bottomRows; i++)
            {
                for (int c = 0; c < displayCols; c++)
                    Msg.Append(" ");
            }
        }
    }

    // Write message to LCD
    LCDWrite(Msg);
}