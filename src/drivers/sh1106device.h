//
// sh1106device.h
//
// Mini-JV880pi - Roland JV880 synthesizer for bare metal Raspberry Pi
// SH1106 128x64 I2C character display driver.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
#ifndef _display_sh1106device_h
#define _display_sh1106device_h

#include <circle/i2cmaster.h>
#include <circle/types.h>
#include <display/chardevice.h>

class CSH1106Device : public CCharDevice
{
public:
	// SH1106 modules normally expose 128x64 visible pixels from a
	// 132x64 internal display RAM. nColumns is normally 20 or 25;
	// nRows is normally 2 or 4 for Mini-JV880pi.
	CSH1106Device (unsigned nColumns, unsigned nRows,
			 CI2CMaster *pI2CMaster, u8 nAddress,
			 bool bRotated = false, bool bMirrored = false);
	~CSH1106Device (void);

	boolean Initialize (void);

private:
	void DevClearCursor (void) override;
	void DevSetCursor (unsigned nCursorX, unsigned nCursorY) override;
	void DevSetCursorMode (boolean bVisible) override;
	void DevSetChar (unsigned nPosX, unsigned nPosY, char chChar) override;
	void DevUpdateDisplay (void) override;

	void WriteCommand (u8 nCommand) const;
	void WriteFrameBuffer (void) const;
	void ClearFrameBuffer (void);
	void DrawChar (char chChar, unsigned nPosX, unsigned nPosY);

private:
	static const unsigned WIDTH = 128;
	static const unsigned HEIGHT = 64;
	static const unsigned PAGE_COUNT = HEIGHT / 8;
	static const unsigned COLUMN_OFFSET = 2;

	unsigned m_nColumns;
	unsigned m_nRows;
	unsigned m_nCellWidth;
	CI2CMaster *m_pI2CMaster;
	u8 m_nAddress;
	bool m_bRotated;
	bool m_bMirrored;
	u8 m_FrameBuffer[WIDTH * HEIGHT / 8];
};

#endif
