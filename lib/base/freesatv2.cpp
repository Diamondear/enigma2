/* 
FreeSat Huffman decoder for VDR
Copyright (C) 2008  DOM http://www.rst38.org.uk/vdr/
Port to C++ / Enigma 2
Copyright (C) 2008  Martin Croome

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "freesatv2.h"
#ifdef FREESATV2_DEBUG
#	include "eerror.h"
#endif
#include <asm/types.h>
#include "cfile.h"

#define START   '\0'
#define STOP    '\0'
#define ESCAPE  '\1'

#ifndef DATADIR
#	define DATADIR "/usr/share"
#endif

#ifndef FREESAT_DATA_DIRECTORY
#define FREESAT_DATA_DIRECTORY       DATADIR
#endif
#define TABLE1_FILENAME FREESAT_DATA_DIRECTORY "/enigma2/freesat.t1"
#define TABLE2_FILENAME FREESAT_DATA_DIRECTORY "/enigma2/freesat.t2"

static void loadFile(huffTableEntry **table, const char *filename);


struct huffTableEntry {
	__u32 value;
	__u16 bits;
	char next;
	huffTableEntry * nextEntry;

	huffTableEntry(unsigned int value, short bits, char next) : value(value), bits(bits), next(next), nextEntry(NULL)
	{ }
};

freesatHuffmanDecoder::freesatHuffmanDecoder()
{
	memset(m_tables, 0, sizeof(m_tables));
	loadFile(&m_tables[0][0], TABLE1_FILENAME);
	loadFile(&m_tables[1][0], TABLE2_FILENAME);
}

freesatHuffmanDecoder::~freesatHuffmanDecoder()
{
	int	i, j;
	huffTableEntry *currentEntry, *nextEntry;
	for ( j = 0 ; j < 2; j++ )
	{ 
		for ( i = 0 ; i < 256; i++ ) 
		{
			currentEntry = m_tables[j][i];
			while ( currentEntry != NULL )
			{
				nextEntry = currentEntry->nextEntry;
				delete currentEntry;
				currentEntry = nextEntry;
			}
			m_tables[j][i] = NULL;
		}
	}
}


/** \brief Convert a textual character description into a value
*
*  \param str - Encoded (in someway) string
*
*  \return Raw character
*/
static unsigned char resolveChar(const char *str)
{
	if (str[1] == 0)
		return str[0];

	switch(str[0])
	{
		case '0':
			{
				int val;
				if ( sscanf(str,"0x%02x", &val) == 1 )
				{
					return val;
				}
			}
			break;
		case 'E':
			if ( strcmp(str,"ESCAPE") == 0 )
				return ESCAPE;
			break;
		case 'S':
			if ( strcmp(str,"STOP") == 0 )
				return STOP;
			if ( strcmp(str,"START") == 0 )
				return START;
			break;
	}
	return str[0];
}


/** \brief Decode a binary string into a value
*
*  \param binary - Binary string to decode
*
*  \return Decoded value
*/
static unsigned long decodeBinary(const char *binary)
{
	unsigned long mask = 0x80000000;
	unsigned long val = 0;

	while (*binary)
	{
		if ( *binary == '1' )
		{
			val |= mask;
		}
		mask >>= 1;
		++binary;
	}
	return val;
}

static void loadFile(huffTableEntry **table, const char *filename)
{
	char buf[1024];
	char *from;
	char *to;
	char *binary;
	char *colon;

	CFile fp(filename, "r");
	if ( fp )
	{
		while ( fgets(buf,sizeof(buf),fp) != NULL )
		{
			// Tokenize string "in place"
			from = buf;
			colon = strchr(buf, ':');
			if (colon == NULL)
				continue;
			binary = colon + 1;
			*colon = 0;
			colon = strchr(binary, ':');
			if (colon == NULL)
				continue;
			*colon = 0;
			to = colon + 1;
			colon = strchr(to, ':');
			if (colon != NULL)
				*colon = 0;
			{
				int bin_len = strlen(binary);
				int from_char = resolveChar(from);
				char to_char = resolveChar(to);
				unsigned long bin = decodeBinary(binary);

				// Add entry to end of bucket
				huffTableEntry **pCurrent = &table[from_char];
				while ( *pCurrent != NULL )
				{
					pCurrent = &((*pCurrent)->nextEntry);
				}
				*pCurrent = new huffTableEntry(bin, bin_len, to_char);
			}
		}
	}
#ifdef FREESATV2_DEBUG
	else
	{
		eDebug("[FREESAT] Cannot load '%s'",filename);
	}
#endif
}


/** \brief Decode an EPG string as necessary
*
*  \param src - Possibly encoded string
*  \param size - Size of the buffer
*
*  \retval NULL - Can't decode
*  \return A decoded string
*/
std::string freesatHuffmanDecoder::decode(const unsigned char *src, size_t size)
{
	std::string uncompressed;

	if (src[0] != 0x1f)
		return uncompressed;
	
	const unsigned int table_index = src[1] - 1;

	if (table_index <= 1)
	{
		huffTableEntry **table = &m_tables[table_index][0];
		unsigned int value = 0;
		unsigned int byte = 2;
		unsigned int bit = 0;
		int lastch = START;

		while (byte < 6 && byte < size)
		{
			value |= src[byte] << ((5-byte) * 8);
			byte++;
		}

		do
		{
			int found = 0;
			unsigned bitShift = 0;
			if (lastch == ESCAPE)
			{
				char nextCh = (value >> 24) & 0xff;
				found = 1;
				// Encoded in the next 8 bits.
				// Terminated by the first ASCII character.
				bitShift = 8;
				if ((nextCh & 0x80) == 0)
					lastch = nextCh;
				uncompressed.append(&nextCh, 1);
			}
			else
			{
				huffTableEntry * currentEntry = table[lastch];
				while ( currentEntry != NULL )
				{
					unsigned mask = 0, maskbit = 0x80000000;
					short kk;
					for ( kk = 0; kk < currentEntry->bits; kk++)
					{
						mask |= maskbit;
						maskbit >>= 1;
					}
					if ((value & mask) == currentEntry->value)
					{
						char nextCh = currentEntry->next;
						bitShift = currentEntry->bits;
						if (nextCh != STOP && nextCh != ESCAPE)
						{
							uncompressed.append(&nextCh, 1);
						}
						found = 1;
						lastch = nextCh;
						break;
					}
					currentEntry = currentEntry->nextEntry;
				}
			}
			if (found)
			{
				// Shift up by the number of bits.
				unsigned b;
				for ( b = 0; b < bitShift; b++)
				{
					value = (value << 1) & 0xfffffffe;
					if (byte < size)
						value |= (src[byte] >> (7-bit)) & 1;
					if (bit == 7)
					{
						bit = 0;
						byte++;
					}
					else bit++;
				}
			}
			else
			{
#ifdef FREESATV2_DEBUG
				eDebug("[FREESAT] Missing table %d entry: <%s>", table_index + 1, uncompressed.c_str());
#endif
				return uncompressed;
			}
		} while (lastch != STOP && value != 0);
	}
	return uncompressed;
}
