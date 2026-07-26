//
// st7920device.h
//
// Mini-JV880pi - Roland JV880 synthesizer for bare metal Raspberry Pi
// ST7920 128x64 serial graphic LCD character-device driver.
//
// The module is used in its native three-wire serial mode:
//   RS/CS   = chip select
//   R/W/SID = serial data
//   E/SCLK  = serial clock
//   PSB     = GND (selected in hardware)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#ifndef _display_st7920device_h
#define _display_st7920device_h

#include <circle/gpiopin.h>
#include <circle/types.h>
#include <display/chardevice.h>

class CST7920Device : public CCharDevice
{
public:
	CST7920Device (unsigned nColumns, unsigned nRows,
			 unsigned nSelectPin, unsigned nDataPin,
			 unsigned nClockPin, unsigned nResetPin = 0,
			 bool bRotated = false, bool bMirrored = false);
	~CST7920Device (void);

	boolean Initialize (void);

private:
	void DevClearCursor (void) override;
	void DevSetCursor (unsigned nCursorX, unsigned nCursorY) override;
	void DevSetCursorMode (boolean bVisible) override;
	void DevSetChar (unsigned nPosX, unsigned nPosY, char chChar) override;
	void DevUpdateDisplay (void) override;

	void WriteRawByte (u8 nByte) const;
	void WriteSerialByte (bool bData, u8 nByte) const;
	void WriteCommand (u8 nCommand) const;
	void WriteData (u8 nData) const;
	void WriteFrameBuffer (void);
	void ClearFrameBuffer (void);
	void DrawChar (char chChar, unsigned nPosX, unsigned nPosY);
	void SetPixel (unsigned nX, unsigned nY, bool bOn);
	void MarkAllRowsDirty (void);

private:
	static const unsigned WIDTH = 128;
	static const unsigned HEIGHT = 64;
	static const unsigned BYTES_PER_ROW = WIDTH / 8;

	unsigned m_nColumns;
	unsigned m_nRows;
	unsigned m_nCellWidth;
	bool m_bRotated;
	bool m_bMirrored;

	CGPIOPin *m_pSelectPin;
	CGPIOPin *m_pDataPin;
	CGPIOPin *m_pClockPin;
	CGPIOPin *m_pResetPin;

	// Linear 128x64 monochrome framebuffer, one bit per pixel.
	u8 m_FrameBuffer[HEIGHT * BYTES_PER_ROW];
	bool m_DirtyRows[HEIGHT];
};

#endif
