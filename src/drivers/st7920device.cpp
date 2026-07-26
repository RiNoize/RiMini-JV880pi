//
// st7920device.cpp
//
// Mini-JV880pi - Roland JV880 synthesizer for bare metal Raspberry Pi
// ST7920 128x64 serial graphic LCD character-device driver.
//
#include "st7920device.h"
#include "font5x8.h"
#include <circle/timer.h>
#include <assert.h>
#include <string.h>

CST7920Device::CST7920Device (unsigned nColumns, unsigned nRows,
				     unsigned nSelectPin, unsigned nDataPin,
				     unsigned nClockPin, unsigned nResetPin,
				     bool bRotated, bool bMirrored)
: CCharDevice (nColumns, nRows),
  m_nColumns (nColumns),
  m_nRows (nRows),
  m_nCellWidth (nColumns >= 24 ? 5 : 6),
  m_bRotated (bRotated),
  m_bMirrored (bMirrored),
  m_pSelectPin (new CGPIOPin (nSelectPin, GPIOModeOutput)),
  m_pDataPin (new CGPIOPin (nDataPin, GPIOModeOutput)),
  m_pClockPin (new CGPIOPin (nClockPin, GPIOModeOutput)),
  m_pResetPin (nResetPin != 0
	? new CGPIOPin (nResetPin, GPIOModeOutput)
	: nullptr),
  m_FrameBuffer {0},
  m_DirtyRows {false}
{
}

CST7920Device::~CST7920Device (void)
{
	delete m_pResetPin;
	delete m_pClockPin;
	delete m_pDataPin;
	delete m_pSelectPin;
}

boolean CST7920Device::Initialize (void)
{
	assert (m_pSelectPin != nullptr);
	assert (m_pDataPin != nullptr);
	assert (m_pClockPin != nullptr);

	if (m_nColumns == 0 || m_nColumns > 25 || m_nRows == 0 || m_nRows > 8)
	{
		return FALSE;
	}

	// ST7920 serial mode accepts clocks only while CS is high. The data
	// sheet recommends leaving SCLK low before CS is deasserted.
	m_pSelectPin->Write (LOW);
	m_pDataPin->Write (LOW);
	m_pClockPin->Write (LOW);

	if (m_pResetPin != nullptr)
	{
		m_pResetPin->Write (LOW);
		CTimer::SimpleMsDelay (2);
		m_pResetPin->Write (HIGH);
	}

	CTimer::SimpleMsDelay (50);

	// Conservative serial-mode initialization for common 12864 modules.
	// PSB must already be tied to GND on the adapter board.
	WriteCommand (0x30);             // Basic instruction set, 8-bit interface
	WriteCommand (0x30);             // Repeat after power-up settling
	WriteCommand (0x0C);             // Display on, cursor and blink off
	WriteCommand (0x01);             // Clear DDRAM
	WriteCommand (0x06);             // Entry mode: increment address
	WriteCommand (0x34);             // Extended instruction set
	WriteCommand (0x36);             // Extended instruction set + graphics on

	ClearFrameBuffer ();
	WriteFrameBuffer ();

	return CCharDevice::Initialize ();
}

void CST7920Device::DevClearCursor (void)
{
	ClearFrameBuffer ();
}

void CST7920Device::DevSetCursor (unsigned nCursorX, unsigned nCursorY)
{
	(void) nCursorX;
	(void) nCursorY;
}

void CST7920Device::DevSetCursorMode (boolean bVisible)
{
	(void) bVisible;
}

void CST7920Device::DevSetChar (unsigned nPosX, unsigned nPosY, char chChar)
{
	DrawChar (chChar, nPosX, nPosY);
}

void CST7920Device::DevUpdateDisplay (void)
{
	WriteFrameBuffer ();
}

void CST7920Device::WriteRawByte (u8 nByte) const
{
	// The ST7920 samples SID on the rising edge of SCLK. Keep clock low
	// between bits and after the final bit, as recommended by the data sheet.
	for (unsigned nBit = 0; nBit < 8; ++nBit)
	{
		m_pDataPin->Write ((nByte & 0x80) != 0 ? HIGH : LOW);
		CTimer::SimpleusDelay (1);
		m_pClockPin->Write (HIGH);
		CTimer::SimpleusDelay (1);
		m_pClockPin->Write (LOW);
		nByte <<= 1;
	}
}

void CST7920Device::WriteSerialByte (bool bData, u8 nByte) const
{
	m_pSelectPin->Write (HIGH);
	CTimer::SimpleusDelay (1);

	// Serial packet: synchronization byte, upper nibble, lower nibble.
	// For writes: 0xF8 selects instruction register, 0xFA data register.
	WriteRawByte (bData ? 0xFA : 0xF8);
	WriteRawByte (nByte & 0xF0);
	WriteRawByte ((nByte << 4) & 0xF0);

	// SCLK is already low here. Deassert CS only after the final bit.
	CTimer::SimpleusDelay (1);
	m_pSelectPin->Write (LOW);

	// Serial mode is write-only and has no busy-flag readback. The ST7920
	// has no instruction queue, so leave conservative execution time.
	CTimer::SimpleusDelay (80);
}

void CST7920Device::WriteCommand (u8 nCommand) const
{
	WriteSerialByte (false, nCommand);

	// Clear display and return home need substantially longer than ordinary
	// instructions. The extra delay is harmless for initialization and reset.
	if (nCommand == 0x01 || nCommand == 0x02)
	{
		CTimer::SimpleusDelay (1600);
	}
}

void CST7920Device::WriteData (u8 nData) const
{
	WriteSerialByte (true, nData);
}

void CST7920Device::WriteFrameBuffer (void)
{
	bool bAnyDirty = false;
	for (unsigned nY = 0; nY < HEIGHT; ++nY)
	{
		if (m_DirtyRows[nY])
		{
			bAnyDirty = true;
			break;
		}
	}

	if (!bAnyDirty)
	{
		return;
	}

	// Re-select extended graphics mode before a refresh. This is useful on
	// modules whose controller was temporarily returned to basic mode.
	WriteCommand (0x36);

	for (unsigned nY = 0; nY < HEIGHT; ++nY)
	{
		if (!m_DirtyRows[nY])
		{
			continue;
		}

		// ST7920 GDRAM is arranged as two 128x32 halves. Rows 32..63
		// repeat vertical addresses 0..31 and start at horizontal address 8.
		const u8 nVerticalAddress = static_cast<u8> (nY & 0x1F);
		const u8 nHorizontalAddress = nY >= 32 ? 8 : 0;

		WriteCommand (0x80 | nVerticalAddress);
		WriteCommand (0x80 | nHorizontalAddress);

		const unsigned nRowOffset = nY * BYTES_PER_ROW;
		for (unsigned nByte = 0; nByte < BYTES_PER_ROW; ++nByte)
		{
			WriteData (m_FrameBuffer[nRowOffset + nByte]);
		}

		m_DirtyRows[nY] = false;
	}
}

void CST7920Device::ClearFrameBuffer (void)
{
	memset (m_FrameBuffer, 0, sizeof m_FrameBuffer);
	MarkAllRowsDirty ();
}

void CST7920Device::DrawChar (char chChar, unsigned nPosX, unsigned nPosY)
{
	if (nPosX >= m_nColumns || nPosY >= m_nRows)
	{
		return;
	}

	if (chChar < ' ')
	{
		chChar = ' ';
	}

	unsigned nCharIndex = static_cast<u8> (chChar - ' ');
	if (nCharIndex >= FONT5X8_SIZE)
	{
		nCharIndex = 0;
	}

	const unsigned nBaseX = nPosX * m_nCellWidth;
	const unsigned nBaseY = nPosY * 8;

	for (unsigned nRow = 0; nRow < 8; ++nRow)
	{
		const unsigned nPhysicalY = m_bRotated
			? HEIGHT - 1 - (nBaseY + nRow)
			: nBaseY + nRow;
		const bool bWasDirty = m_DirtyRows[nPhysicalY];
		u8 OldRow[BYTES_PER_ROW];
		memcpy (OldRow, &m_FrameBuffer[nPhysicalY * BYTES_PER_ROW],
			BYTES_PER_ROW);

		// Clear the complete character cell first, including its optional
		// sixth spacing column in 20-column mode.
		for (unsigned nColumn = 0; nColumn < m_nCellWidth; ++nColumn)
		{
			SetPixel (nBaseX + nColumn, nBaseY + nRow, false);
		}

		const u8 nRowBits = Font5x8[nCharIndex][nRow];
		for (unsigned nColumn = 0; nColumn < 5; ++nColumn)
		{
			if ((nRowBits & (1u << (4 - nColumn))) != 0)
			{
				SetPixel (nBaseX + nColumn, nBaseY + nRow, true);
			}
		}

		m_DirtyRows[nPhysicalY] = bWasDirty
			|| memcmp (OldRow,
				   &m_FrameBuffer[nPhysicalY * BYTES_PER_ROW],
				   BYTES_PER_ROW) != 0;
	}
}

void CST7920Device::SetPixel (unsigned nX, unsigned nY, bool bOn)
{
	if (nX >= WIDTH || nY >= HEIGHT)
	{
		return;
	}

	if (m_bRotated)
	{
		nX = WIDTH - 1 - nX;
		nY = HEIGHT - 1 - nY;
	}

	if (m_bMirrored)
	{
		nX = WIDTH - 1 - nX;
	}

	const unsigned nOffset = nY * BYTES_PER_ROW + nX / 8;
	const u8 nMask = static_cast<u8> (0x80 >> (nX & 7));

	const u8 nOldValue = m_FrameBuffer[nOffset];
	u8 nNewValue = nOldValue;

	if (bOn)
	{
		nNewValue |= nMask;
	}
	else
	{
		nNewValue &= static_cast<u8> (~nMask);
	}

	if (nNewValue != nOldValue)
	{
		m_FrameBuffer[nOffset] = nNewValue;
		m_DirtyRows[nY] = true;
	}
}

void CST7920Device::MarkAllRowsDirty (void)
{
	for (unsigned nY = 0; nY < HEIGHT; ++nY)
	{
		m_DirtyRows[nY] = true;
	}
}
