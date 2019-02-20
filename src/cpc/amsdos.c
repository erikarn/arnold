/* 
 *  Arnold emulator (c) Copyright, Kevin Thacker 1995-2001
 *  
 *  This file is part of the Arnold emulator source code distribution.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/* AMSDOS helper functions */

#include "amsdos.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "diskimage/diskimg.h"

static AMSDOS_FORMAT AmsdosFormat_SYSTEM={0x041,2,9};
static AMSDOS_FORMAT AmsdosFormat_DATA={0x0c1,0,9};
static AMSDOS_FORMAT AmsdosFormat_IBM={0x01,1,8};

static BOOL	AMSDOS_CheckValidFilename(const amsdos_directory_entry *entry);
static BOOL	AMSDOS_CheckValidLength(const amsdos_directory_entry *entry);
static void	AMSDOS_GetFilenameFromEntry(const amsdos_directory_entry *entry, char *Filename);
static BOOL	AMSDOS_DoesDirectoryEntryQualifyForAutorun(const amsdos_directory_entry *entry);
static void	AMSDOS_ConvertBlockToTrackSectorSide(const AMSDOS_FORMAT *pFormat, int nBlock, AMSDOS_TRACK_SECTOR_SIDE *pTSS);
static BOOL AMSDOS_HasDirectory(int nDrive, const AMSDOS_FORMAT *pFormat);
static BOOL AMSDOS_SectorExists(int nDrive, int nTrack, int nID);
static void AMSDOS_ReadDirectory(unsigned char *pDirectory, int nDrive, int nTrack, int nID);
static int AMSDOS_GetSectorIndex(int nDrive, int nTrack, int nID);
static BOOL AMSDOS_HasDirectory(int nDrive, const AMSDOS_FORMAT *pFormat);
static BOOL AMSDOS_GetSector(int nDrive, int nTrack, int nSector, unsigned char *pBuffer);
static BOOL AMSDOS_DoesFileQualifyForAutorun(const AMSDOS_FORMAT *pFormat, const amsdos_directory_entry *entry, unsigned char *pBuffer);
static BOOL AMSDOS_ProcessDirectory(const AMSDOS_FORMAT *pFormat,unsigned char *pBuffer, char *AutorunCommand);
static BOOL AMSDOS_IsBootable(int nDrive, unsigned char *pBuffer);
static int AMSDOS_GetPrefixPriority(const char *pFilename);
static int AMSDOS_GetFilenamePriority(const char *pFilename);

/*--------------------------------------------------------------------------------------*/

/* calculate checksum as AMSDOS would for the first 66 bytes of a datablock */
/* this is used to determine if a file has a AMSDOS header */
unsigned int AMSDOS_CalculateChecksum(const unsigned char *pHeader)
{
        unsigned int Checksum;
        int i;

        Checksum = 0;

        for (i=0; i<67; i++)
        {
                unsigned int CheckSumByte;

                CheckSumByte = pHeader[i] & 0x0ff;

                Checksum+=CheckSumByte;
        }

        return Checksum;
}

/*--------------------------------------------------------------------------------------*/

BOOL     AMSDOS_HasAmsdosHeader(const unsigned char *pHeader)
{
        unsigned int CalculatedChecksum;
        unsigned int ChecksumFromHeader;

        CalculatedChecksum = AMSDOS_CalculateChecksum(pHeader);

        ChecksumFromHeader = (pHeader[67] & 0x0ff) |
                                                (pHeader[68] & 0x0ff)<<8;

        if (ChecksumFromHeader == CalculatedChecksum)
                return TRUE;

        return FALSE;
}

/*--------------------------------------------------------------------------------------*/

BOOL	AMSDOS_IsValidFilenameCharacter(char ch)
{
	/* valid characters are:
		A-Z 0-9 ! " # $ & ' + - @ ^ ' } {
	*/

	if ((ch>='A') && (ch<='Z'))
		return TRUE;
	if ((ch>='0') && (ch<='9'))
		return TRUE;
	if (ch=='!')
		return TRUE;
	if (ch=='"')			/* really?? */
		return TRUE;
	if (ch=='#')
		return TRUE;
	if (ch=='$')
		return TRUE;
	if (ch=='&')
		return TRUE;
	if (ch=='\'')
		return TRUE;
	if (ch=='+')
		return TRUE;
	if (ch=='-')
		return TRUE;
	if (ch=='@')
		return TRUE;
	if (ch=='^')
		return TRUE;
	if (ch=='}')
		return TRUE;
	if (ch=='{')
		return TRUE;

	return FALSE;

}

/*--------------------------------------------------------------------------------------*/

/* checks if the filename is a valid filename that can be typed by the user
and which, if present on the disc, AMSDOS can actually load.

Some discs have filenames with control characters. These are used to create
a picture when the disc is catalogued. These can't be typed and will not be valid 
to run. */
static BOOL	AMSDOS_CheckValidFilename(const amsdos_directory_entry *entry)
{
	int nPos = 0;
	char ch;

	do
	{
		ch = entry->Filename[nPos];
		nPos++;

		if (ch!=' ')
		{
			if (!AMSDOS_IsValidFilenameCharacter(ch))
				return FALSE;
		}
	}
	while ((nPos!=8) && (ch!=' '));

	/* filename with space as first character?*/
	if (nPos==1)
		return FALSE;

	if (nPos!=8) 
	{
		/* assumption is we have seen a space character, so check
		the remaining characters are also spaces. i.e. the filename
		has been padded with spaces */

		do
		{
			/* get this character */
			ch = entry->Filename[nPos];
			nPos++;
		}
		while ((ch==' ') && (nPos!=8));

		/* if we have found a character other than space, then the
		position will not be at the end of the filename part! */

		if (nPos!=8)
			return FALSE;
	}

	/* filename part is valid */

	nPos = 0;
	/* check extension part of filename */
	do
	{
		/* get character and remove top bit; flag bit used by AMSDOS and CPM */
		ch = entry->Extension[nPos]&0x07f;
		nPos++;

		if (ch!=' ')
		{
			if (!AMSDOS_IsValidFilenameCharacter(ch))
				return FALSE;
		}
	}
	while ((nPos!=3) && (ch!=' '));

	if (nPos!=3)
	{
		/* assumption is we have seen a space character, so check
		the remaining characters are also spaces. i.e. the filename
		has been padded with spaces */

		do
		{
			char ch;

			/* get this character */
			ch = entry->Filename[nPos];
			nPos++;
		}
		while ((ch==' ') && (nPos!=3));

		/* if we have found a character other than space, then the
		position will not be at the end of the filename part! */

		if (nPos!=3)
			return FALSE;
	}

	return TRUE;
}

/*--------------------------------------------------------------------------------------*/

/* check that the file has a valid length according to the directory entry */
/* this works for all files */
static BOOL	AMSDOS_CheckValidLength(const amsdos_directory_entry *entry)
{
	int nValidBlocks = 0;
	int i;

	/* must have a valid length in records */
	if (entry->LengthInRecords==0)
		return FALSE;

	/* must have at least one valid block */
	/* The following assumes standard AMSDOS disc formats: DATA, SYSTEM, IBM */
	i=0;

	while (i<16)
	{
		int nBlock;

		nBlock = entry->Blocks[i];

		if (nBlock!=0)
			nValidBlocks++;
	
		/* a block value of zero is used to terminate the block list */
		if (nBlock==0)
			break;

		i++;
	}

	/* if there is at least one valid block, the file has a valid size */
	return (nValidBlocks!=0);
}

/*--------------------------------------------------------------------------------------*/
/* extracts the filename from the given directory entry */
/* puts it into the output buffer and makes it "nice" */
static void	AMSDOS_GetFilenameFromEntry(const amsdos_directory_entry *entry, char *Filename)
{
	int nPos;
	int nOutPos = 0;

	/* if filename or extension part is not fully used (it is padded
	with spaces) then do not get the padding */

	/* get filename characters */
	nPos = 0;
	do
	{
		char ch;

		/* get character and remove top-bit (flag bit used by AMSDOS/CPM) */
		ch=entry->Filename[nPos];
		nPos++;

		if (ch==' ')
			break;

		/* convert to upper case */
		ch = (char)toupper((int)ch);

		/* output to filename buffer */
		Filename[nOutPos] = ch;
		nOutPos++;
	}
	while (nPos!=8);

	Filename[nOutPos]='.';
	nOutPos++;

	/* get extension */

	nPos = 0;
	do
	{
		char ch;

		/* get character and remove top-bit (flag bit used by AMSDOS/CPM) */
		ch = entry->Extension[nPos]&0x07f;
		nPos++;

		if (ch==' ')
			break;

		/* convert to upper case */
		ch = (char)toupper((int)ch);

		Filename[nOutPos] = ch;
		nOutPos++;
	}
	while (nPos!=3);


	Filename[nOutPos] = '\0';
}


/*--------------------------------------------------------------------------------------*/
/* convert block number to track, sector, side */
void	AMSDOS_ConvertBlockToTrackSectorSide(const AMSDOS_FORMAT *pFormat, int nBlock, AMSDOS_TRACK_SECTOR_SIDE *pTSS)
{
	/* for standard AMSDOS formats there are 2 sectors per block */
	/* convert block to sector offset */
	int nSectorOffset = (nBlock*2);
	/* convert sector offset to track */
	int nTrack = nSectorOffset/pFormat->nSectorsPerTrack;
	/* this is the sector within the track */
	int nSector = nSectorOffset%pFormat->nSectorsPerTrack;

	/* store values */
	pTSS->nSector = nSector + pFormat->nFirstSectorId;
	pTSS->nTrack = nTrack + pFormat->nReservedTracks;
	/* for standard AMSDOS formats there is a single side */
	pTSS->nSide = 0;
}


/*--------------------------------------------------------------------------------------*/

/* checks this directory entry. Returns TRUE if filename is suitable for autorun,
FALSE otherwise. The following assumes standard AMSDOS disc formats: DATA, SYSTEM, IBM */
BOOL	AMSDOS_DoesDirectoryEntryQualifyForAutorun(const amsdos_directory_entry *entry)
{
	/* file must be in user 0 */
	if (entry->UserNumber!=0)
		return FALSE;

	/* must be first extent of file */
	if (entry->Extent!=0)
		return FALSE;

	/* file must be visible in directory listing */

	/* bit 7 of Extension[0] is set if file is read-only */
	/* bit 7 of Extension[1] is set if file is system (hidden from directory listing) */
	if ((entry->Extension[1]&0x080)!=0)
		return FALSE;

	/* check valid length (doesn't look at header) */
	if (!AMSDOS_CheckValidLength(entry))
		return FALSE;

	/* check the filename can actually be typed */
	if (!AMSDOS_CheckValidFilename(entry))
		return FALSE;

	return TRUE;
}


/*--------------------------------------------------------------------------------------*/

BOOL AMSDOS_SectorExists(int nDrive, int nTrack, int nID)
{
	return (AMSDOS_GetSectorIndex(nDrive, nTrack, nID)!=-1);
}

/*--------------------------------------------------------------------------------------*/
int AMSDOS_GetSectorIndex(int nDrive, int nTrack, int nID)
{
	int i;
	int nSectors = DiskImage_GetSectorsPerTrack(nDrive, nTrack, 0);

	for (i=0; i<nSectors; i++)
	{
		CHRN chrn;

		DiskImage_GetID(nDrive, nTrack,0, i, &chrn);
		if ((chrn.N == 0x02) && (chrn.R == nID) && (chrn.H==0))
			return i;
	}
	return -1;
}

/*--------------------------------------------------------------------------------------*/
BOOL AMSDOS_GetSector(int nDrive, int nTrack, int nSector, unsigned char *pBuffer)
{
	int nSectorIndex = AMSDOS_GetSectorIndex(nDrive,nTrack, nSector);

	if (nSectorIndex==-1)
		return FALSE;

	DiskImage_GetSector(nDrive, nTrack, 0, nSectorIndex, (char *)pBuffer);

	return TRUE;
}

/*--------------------------------------------------------------------------------------*/
void AMSDOS_ReadDirectory(unsigned char *pDirectory, int nDrive, int nTrack, int nID)
{
	int i;

	for (i=0; i<4; i++)
	{
		AMSDOS_GetSector(nDrive, nTrack, nID+i, pDirectory);
		pDirectory+=512;
	}
}
/*--------------------------------------------------------------------------------------*/
/* is disc bootable using |CPM? */
BOOL AMSDOS_IsBootable(int nDrive, unsigned char *pBuffer)
{
	char ch;
	int i;

	/* |CPM boot only works from drive 0 */
	if (nDrive!=0)
		return FALSE;

	/* read sector into buffer */
	if (!AMSDOS_GetSector(nDrive,0, 0x041, pBuffer))
		return FALSE;

	/* check sector has some data; i.e. it is not all the same byte */
	ch = pBuffer[0];

	for (i=1; i<512; i++)
	{
		if (pBuffer[i]!=ch)
			return TRUE;
	}

	return FALSE;
}

/*--------------------------------------------------------------------------------------*/
BOOL AMSDOS_DoesFileQualifyForAutorun(const AMSDOS_FORMAT *pFormat, const amsdos_directory_entry *entry, unsigned char *pBuffer)
{
	/* now load the header */
	int nFirstBlock;
	AMSDOS_TRACK_SECTOR_SIDE TSS;
	int nDrive = 0;

	nFirstBlock = entry->Blocks[0];

	/* convert block to track sector and side */
	AMSDOS_ConvertBlockToTrackSectorSide(pFormat, nFirstBlock, &TSS);

	/* read sector into buffer */
	if (!AMSDOS_GetSector(nDrive,TSS.nTrack, TSS.nSector, pBuffer))
		return FALSE;

	/* has a header? */
	
	/* it is possible to run BASIC programs written as ASCII, but we'll ignore these
	for now as these are unlikely */
	if (!AMSDOS_HasAmsdosHeader((const unsigned char *)pBuffer))
	{
		return FALSE;
	}
	else
	{
		const AMSDOS_HEADER *pHeader = (const AMSDOS_HEADER *)pBuffer;
		unsigned long nLength;

		nLength = (pHeader->LogicalLengthLow&0x0ff) |
			((pHeader->LogicalLengthHigh&0x0ff)<<8);

		/* if header reports length as 0, then there is nothing to run. */
		if (nLength==0)
			return FALSE;

		/* check additional parameters based on file type */
		switch (pHeader->FileType&0x0fe)
		{
			/* BASIC */
			case (0<<1):
			{
				/* check for empty BASIC programs?? */
				/* or is this going too far? */
			}
			break;

			/* BINARY */
			case (1<<1):
			{
				/* check that the execution address is within the limits
				of the file */
				unsigned long nExecutionAddress;
				unsigned long nLoadAddress;

				/* get execution address */
				nExecutionAddress = ((pHeader->ExecutionAddressLow&0x0ff) | ((pHeader->ExecutionAddressHigh&0x0ff)<<8));
				
				/* get load address */
				nLoadAddress = ((pHeader->LocationLow&0x0ff) | ((pHeader->LocationHigh & 0x0ff)<<8));

				if (nExecutionAddress<nLoadAddress)
					return FALSE;

				if ((nExecutionAddress-nLoadAddress)>nLength)
					return FALSE;
			}
			break;

			default:
				return FALSE;
		}
	}

	return TRUE;
}



/*--------------------------------------------------------------------------------------*/
/* If no prefix is given, then AMSDOS will try to match a file using
3 default prefixes '   ','BAS','BIN'. Give these prefixes a higher priority
compared to other prefixes */
int AMSDOS_GetPrefixPriority(const char *pFilename)
{
	int i;
	int nLength = strlen(pFilename);
	const char *pExtension = NULL;

	/* get pointer to extension */
	for (i=nLength-1; i>=0; i--)
	{
		if (pFilename[i]=='.')
		{
			pExtension = &pFilename[i+1];
			break;
		}
	}

	if (pExtension!=NULL)
	{
		/* compare against default prefixes */

		/* default prefixes in order searched for by AMSDOS */
		/* assign higher priority to order prefixes are used */
		if (stricmp(pExtension,"   ")==0)
			return 3;
		if (stricmp(pExtension,"BAS")==0)
			return 2;
		if (stricmp(pExtension,"BIN")==0)
			return 1;
	}

	return 0;
}

/*--------------------------------------------------------------------------------------*/
/* the most common names used to start a disc are "DISC","DISK" and "MENU" */
/* assign a priority to the filename based on this */
int AMSDOS_GetFilenamePriority(const char *pFilename)
{
	int i;
	int nLength = strlen(pFilename);
	int nFilenameLength = nLength;

	/* find length of name part of filename */
	for (i=0; i<nLength; i++)
	{
		if (pFilename[i]=='.')
		{
			nFilenameLength = i;
			break;
		}
	}

	/* compare against common names and assign a priority */
	if (strncmp(pFilename,"DISC",nFilenameLength)==0)
		return 6;
	if (strncmp(pFilename,"DISK",nFilenameLength)==0)
		return 6;
	if (strncmp(pFilename,"MENU",nFilenameLength)==0)
		return 1;
	
	return 0;
}

/*--------------------------------------------------------------------------------------*/
typedef struct
{
	int nEntry;			/* index of the entry in the directory */
	int nPriority;		/* a priority */
} amsdos_valid_directory_entry;

/*--------------------------------------------------------------------------------------*/
static int AMSDOS_CompareValidEntries(const void *elem1, const void *elem2)
{
	const amsdos_valid_directory_entry *entry1 = (const amsdos_valid_directory_entry *)elem1;
	const amsdos_valid_directory_entry *entry2 = (const amsdos_valid_directory_entry *)elem2;

	/* sort into decreasing order */
	return (entry2->nPriority-entry1->nPriority);
}
/*--------------------------------------------------------------------------------------*/

int AMSDOS_ProcessFiles(const AMSDOS_FORMAT *pFormat,unsigned char *pBuffer, char *AutorunCommand)
{
	int i;
	int nValidEntry = -1;
	int nValidEntries = 0;
	amsdos_valid_directory_entry ValidEntries[64];
	int nEntries = 64;
	int nDrive = 0;
	amsdos_directory_entry *entry;
	unsigned char *pDirectory = pBuffer+512;

	/* read the directory; assumption directory exists and is readable */
	AMSDOS_ReadDirectory(pDirectory, nDrive, pFormat->nReservedTracks, pFormat->nFirstSectorId);

	/* process entries */
	entry = (amsdos_directory_entry *)pDirectory;
	
	for (i=0; i<nEntries; i++)
	{
		/* check directory entry is ok */
		if (AMSDOS_DoesDirectoryEntryQualifyForAutorun(entry))
		{
			if (AMSDOS_DoesFileQualifyForAutorun(pFormat, entry,pBuffer))
			{	
				ValidEntries[nValidEntries].nEntry = i;
				nValidEntries++;
			}
		}
		entry++;
	}



	if (nValidEntries==1)
	{
		/* only one entry, so use that */
		nValidEntry = ValidEntries[0].nEntry;
	}
	else
	{
		int i;
		/* multiple valid entries */

		/* assign priorities */
		for (i=0; i<nValidEntries; i++)
		{
			int nPriority = 1;
			char Filename[13];
	
			entry = ((amsdos_directory_entry *)pDirectory)+ValidEntries[i].nEntry;

			AMSDOS_GetFilenameFromEntry(entry, Filename);

			nPriority = AMSDOS_GetPrefixPriority(Filename);
			nPriority += (AMSDOS_GetFilenamePriority(Filename)*3);
			ValidEntries[i].nPriority = nPriority;
		}

		/* sort in order of priority */
		qsort(ValidEntries, nValidEntries,sizeof(amsdos_valid_directory_entry), AMSDOS_CompareValidEntries);

		/* check if there is more than one file with the same priority,
		if there is then we can't autorun the disc :( */
		if (ValidEntries[0].nPriority==ValidEntries[1].nPriority)
			return AUTORUN_TOO_MANY_POSSIBILITIES;
	
		nValidEntry = ValidEntries[0].nEntry;
	}

	if (nValidEntry!=-1)
	{
		char Filename[13];
		entry = ((amsdos_directory_entry *)pDirectory)+nValidEntry;

		AMSDOS_GetFilenameFromEntry(entry, Filename);
		
		/* this file can be autorun */	
		sprintf(AutorunCommand,"RUN\"%s\n",Filename);
	
		return TRUE;
	}


	return AUTORUN_NO_FILES_QUALIFY;
}

/*--------------------------------------------------------------------------------------*/
/* returns TRUE if all the sectors for the directory are readable */
BOOL AMSDOS_HasDirectory(int nDrive, const AMSDOS_FORMAT *pFormat)
{
	int i;

	/* standard AMSDOS formats always have 4 sectors for the directory */
	for (i=0; i<4; i++)
	{
		if (!AMSDOS_SectorExists(nDrive, pFormat->nReservedTracks, pFormat->nFirstSectorId+i))
		{
			return FALSE;
		}
	}

	return TRUE;
}
/*--------------------------------------------------------------------------------------*/

/* Autorun will only check drive 0:

  - |CPM command only works with drive 0
  - a lot of programs do not run from drive B successfully anyway

  returns TRUE if disc can be autorun, FALSE otherwise

  pBuffer points to a buffer of at least 5*512 bytes long!
*/


BOOL AMSDOS_GenerateAutorunCommand(unsigned char *pBuffer, char *AutorunCommand)
{
	BOOL Sector41Exists = FALSE;
	BOOL SectorC1Exists = FALSE;
//	BOOL Sector01Exists = FALSE;
	BOOL HasDirectoryC1 = FALSE;
	BOOL HasDirectory41 = FALSE;
//	BOOL HasDirectory01 = FALSE;
	int nDrive = 0;

	/* check if track 0, sector 41 exists */
	Sector41Exists = AMSDOS_SectorExists(nDrive,0,0x041);

	/* check if track 0, sector c1 exists */
	SectorC1Exists = AMSDOS_SectorExists(nDrive,0,0x0c1);

//	/* check if track 0, sector 01 exists */
//	Sector01Exists = AMSDOS_SectorExists(nDrive,0,0x001);

	if (Sector41Exists)
	{
		HasDirectory41 = AMSDOS_HasDirectory(nDrive, &AmsdosFormat_SYSTEM);
	}

	if (SectorC1Exists)
	{
		HasDirectoryC1 = AMSDOS_HasDirectory(nDrive, &AmsdosFormat_DATA);
	}

//	if (Sector01Exists)
//	{
//		HasDirectory01 = AMSDOS_HasDirectory(nDrive, &AmsdosFormat_IBM);
//	}

	if ((Sector41Exists) && (!SectorC1Exists)/* && (!Sector01Exists)*/)
	{
		/* Sector 41 exists, potentially |CPM bootable */
		/* Sector C1 doesn't exist */
		/* Sector 01 doesn't exist */

		/* disc may be bootable directly through |CPM, or |CPM may be secondary
		run command and you should really run a file in the directory first */

		/* has a directory? */
		if (HasDirectory41)
		{
			/* try to process directory */
			if (AMSDOS_ProcessFiles(&AmsdosFormat_SYSTEM,pBuffer, AutorunCommand))
				return TRUE;
		}

		/* either doesn't have a directory, or failed to find a runnable file in directory
		try CPM boot */

		if (AMSDOS_IsBootable(nDrive, pBuffer))
		{
			strcpy(AutorunCommand,"|CPM\n");
			return TRUE;
		}
	}
	else if ((Sector41Exists) && (SectorC1Exists)/* && (!Sector01Exists)*/)
	{
		/* Sector 41 exists, potentially |CPM bootable */
		/* Sector C1 exists, potentially has a directory */
		/* Sector 01 doesn't exist */

		/* disc may be bootable directly through |CPM, or |CPM may be secondary
		run command and you should really run a file in the directory first */
		if (HasDirectoryC1)
		{
			/* try to process directory */
			int nStatus = AMSDOS_ProcessFiles(&AmsdosFormat_DATA,pBuffer, AutorunCommand);

			if (nStatus==AUTORUN_OK)
				return AUTORUN_OK;
		}

		if (AMSDOS_IsBootable(nDrive, pBuffer))
		{
			strcpy(AutorunCommand,"|CPM\n");
			return AUTORUN_OK;
		}
	}
	else if ((!Sector41Exists) && (SectorC1Exists)/* && (!Sector01Exists)*/)
	{
		/* Sector 41 doesn't exist */
		/* Sector C1 does exist */
		/* Sector 01 doesn't exist */

		if (!HasDirectoryC1)
			return AUTORUN_NOT_POSSIBLE;

		/* disc must have a directory for this disc to autorun */
		return AMSDOS_ProcessFiles(&AmsdosFormat_DATA, pBuffer,AutorunCommand);
	}

	return AUTORUN_NOT_POSSIBLE;
}
