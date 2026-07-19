//
// sh1106device.cpp
//
// Mini-JV880pi - Roland JV880 synthesizer for bare metal Raspberry Pi
// SH1106 128x64 I2C character display driver.
//
// The SH1106 differs from the SSD1306 in two important ways:
//   * it has 132 columns of internal RAM for a 128-pixel panel;
//   * framebuffer data is written one page at a time.
//
#include "sh1106device.h"
#include "font5x8.h"
#include <assert.h>
#include <string.h>

namespace
{
	using CharData = u8[8];

	static constexpr u8 SingleColumn (const CharData& CharData, u8 nColumn)
	{
		u8 nBit = 4 - nColumn;
		u8 nResult = 0;

		for (u8 nRow = 0; nRow < 8; ++nRow)
		{
			nResult |= ((CharData[nRow] >> nBit) & 1) << nRow;
		}

		return nResult;
	}

	template<size_t N, class F>
	class TFont
	{
	public:
		using TColumnData = u8[5];

		constexpr TFont (const CharData (&CharData)[N], F Function)
		: m_CharData {{0}}
		{
			for (size_t nChar = 0; nChar < N; ++nChar)
			{
				for (u8 nColumn = 0; nColumn < 5; ++nColumn)
				{
					m_CharData[nChar][nColumn] = Function (CharData[nChar], nColumn);
				}
			}
		}

		const TColumnData& operator[] (size_t nIndex) const
		{
			return m_CharData[nIndex];
		}

	private:
		TColumnData m_CharData[N];
	};

	constexpr auto FontSingle = TFont<FONT5X8_SIZE, decltype(SingleColumn)> (Font5x8, SingleColumn);
}

CSH1106Device::CSH1106Device (unsigned nColumns, unsigned nRows,
				      CI2CMaster *pI2CMaster, u8 nAddress,
				      bool bRotated, bool bMirrored)
: CCharDevice (nColumns, nRows),
  m_nColumns (nColumns),
  m_nRows (nRows),
  m_nCellWidth (nColumns >= 24 ? 5 : 6),
  m_pI2CMaster (pI2CMaster),
  m_nAddress (nAddress),
  m_bRotated (bRotated),
  m_bMirrored (bMirrored),
  m_FrameBuffer {0}
{
}

CSH1106Device::~CSH1106Device (void)
{
}

boolean CSH1106Device::Initialize (void)
{
	assert (m_pI2CMaster != nullptr);

	if (m_nColumns == 0 || m_nColumns > 25 || m_nRows == 0 || m_nRows > PAGE_COUNT)
	{
		return FALSE;
	}

	const u8 nSegmentRemap =
		(m_bRotated && !m_bMirrored) || (!m_bRotated && m_bMirrored) ? 0xA0 : 0xA1;
	const u8 nCOMScanDirection = m_bRotated ? 0xC0 : 0xC8;

	// Common initialization sequence for 128x64 SH1106 I2C modules.
	const u8 InitSequence[] =
	{
		0xAE,             // Display off
		0xD5, 0x80,       // Display clock divide / oscillator
		0xA8, 0x3F,       // Multiplex ratio: 64
		0xD3, 0x00,       // Display offset
		0x40,             // Display start line 0
		0xAD, 0x8B,       // Internal DC-DC converter on
		nSegmentRemap,
		nCOMScanDirection,
		0xDA, 0x12,       // COM pins configuration
		0x81, 0x80,       // Contrast
		0xD9, 0x22,       // Pre-charge period
		0xDB, 0x35,       // VCOM deselect level
		0xA4,             // Resume RAM display
		0xA6,             // Normal (not inverted) display
		0xAF              // Display on
	};

	for (u8 nCommand : InitSequence)
	{
		WriteCommand (nCommand);
	}

	ClearFrameBuffer ();
	WriteFrameBuffer ();

	return CCharDevice::Initialize ();
}

void CSH1106Device::DevClearCursor (void)
{
	ClearFrameBuffer ();
}

void CSH1106Device::DevSetCursor (unsigned nCursorX, unsigned nCursorY)
{
	(void) nCursorX;
	(void) nCursorY;
}

void CSH1106Device::DevSetCursorMode (boolean bVisible)
{
	(void) bVisible;
}

void CSH1106Device::DevSetChar (unsigned nPosX, unsigned nPosY, char chChar)
{
	DrawChar (chChar, nPosX, nPosY);
}

void CSH1106Device::DevUpdateDisplay (void)
{
	WriteFrameBuffer ();
}

void CSH1106Device::WriteCommand (u8 nCommand) const
{
	const u8 Buffer[] = {0x80, nCommand};
	m_pI2CMaster->Write (m_nAddress, Buffer, sizeof Buffer);
}

void CSH1106Device::WriteFrameBuffer (void) const
{
	// SH1106 uses page addressing. Each visible page is positioned at
	// internal RAM column 2, leaving the controller's two hidden columns
	// on each side outside the 128-pixel panel.
	for (unsigned nPage = 0; nPage < PAGE_COUNT; ++nPage)
	{
		WriteCommand (0xB0 | nPage);
		WriteCommand (0x00 | (COLUMN_OFFSET & 0x0F));
		WriteCommand (0x10 | ((COLUMN_OFFSET >> 4) & 0x0F));

		u8 Packet[WIDTH + 1];
		Packet[0] = 0x40;
		memcpy (&Packet[1], &m_FrameBuffer[nPage * WIDTH], WIDTH);
		m_pI2CMaster->Write (m_nAddress, Packet, sizeof Packet);
	}
}

void CSH1106Device::ClearFrameBuffer (void)
{
	memset (m_FrameBuffer, 0, sizeof m_FrameBuffer);
}

void CSH1106Device::DrawChar (char chChar, unsigned nPosX, unsigned nPosY)
{
	if (nPosX >= m_nColumns || nPosY >= m_nRows)
	{
		return;
	}

	if (chChar < ' ')
	{
		chChar = ' ';
	}

	size_t nCharIndex = static_cast<u8> (chChar - ' ');
	if (nCharIndex >= FONT5X8_SIZE)
	{
		nCharIndex = 0;
	}

	const unsigned nBase = nPosY * WIDTH + nPosX * m_nCellWidth;

	for (unsigned nColumn = 0; nColumn < m_nCellWidth; ++nColumn)
	{
		const unsigned nOffset = nBase + nColumn;
		if (nOffset >= sizeof m_FrameBuffer)
		{
			break;
		}

		// The sixth column in 20-column mode is character spacing.
		m_FrameBuffer[nOffset] = nColumn < 5 ? FontSingle[nCharIndex][nColumn] : 0x00;
	}
}
