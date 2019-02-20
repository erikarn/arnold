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
#include "filetool.h"
#include "../cpc/cpcglob.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cpc/host.h"


#ifdef _WINDOWS
#define DIRECTORY_SEPERATOR '\\'
#else
#define DIRECTORY_SEPERATOR '/'
#endif

char	*GetExtensionFromFilename(char *Filename)
{
	int	i;

	char *pExtension = NULL;	
	int FilenameLength = strlen(Filename);

	if (FilenameLength!=0)
	{
		for (i=FilenameLength-1; i>=0; i--)
		{
			char ch;

			ch = Filename[i];

			if (ch=='.')
			{
				pExtension = &Filename[i+1];
				break;
			}
		}
	}

	return pExtension;
}


BOOL	CheckFileCanBeOpened(char *Filename)
{
	HOST_FILE_HANDLE FileHandle;
	
	/* attempt to open file for reading */
	FileHandle = Host_OpenFile(Filename, HOST_FILE_ACCESS_READ);
	
	/* was it opened? */
	if (FileHandle!=0)
	{
		/* file opened */

		/* close file */
		Host_CloseFile(FileHandle);
		
		/* file exists */
		return TRUE;
	}

	/* file didn't open, either exists, or we are not allowed to open it */
	return FALSE;
}

static char FilenameBuffer[256];

char	*GetFilenameFromPathAndFilename(char *PathAndFilename)
{
	int FilenameOffset;

	int PathAndFilenameLength = strlen(PathAndFilename);

	if (PathAndFilenameLength == 0)
		return NULL;

	FilenameOffset = PathAndFilenameLength-1;

	do
	{
		if (FilenameOffset == -1)
		{
			/* reached start of string, PathAndFilename contains filename only */
		
			FilenameOffset++;
			break;
		}
		else
		{
			char ch;

			/* get character */
			ch = PathAndFilename[FilenameOffset];

			if (ch==DIRECTORY_SEPERATOR)
			{
				/* got directory seperator */
				FilenameOffset++;
				break;
			}
		}
		
		FilenameOffset--;
	}
	while (FilenameOffset>=0);

	if (FilenameOffset<0)
		FilenameOffset = 0;

	/* copy filename into our buffer */
	memcpy(FilenameBuffer, &PathAndFilename[FilenameOffset],PathAndFilenameLength-FilenameOffset);
	
	FilenameBuffer[PathAndFilenameLength-FilenameOffset] = '\0';

	return FilenameBuffer;
}

char	*GetPathFromPathAndFilename(char *PathAndFilename)
{
	int FilenameOffset;

	int PathAndFilenameLength = strlen(PathAndFilename);

	FilenameOffset = PathAndFilenameLength-1;

	do
	{
		if (FilenameOffset == -1)
		{
			/* reached start of string, PathAndFilename contains filename only */
		
			FilenameOffset++;
			break;
		}
		else
		{
			char ch;

			/* get character */
			ch = PathAndFilename[FilenameOffset];

			if (ch==DIRECTORY_SEPERATOR)
			{
				/* got directory seperator */
				FilenameOffset++;
				break;
			}
		}
		
		FilenameOffset--;
	}
	while (FilenameOffset>=0);

	if (FilenameOffset<0)
	{
		FilenameOffset = 0;
	}
	if (FilenameOffset>=256)
	{
		FilenameOffset = 255;
	}

	memcpy(FilenameBuffer, PathAndFilename, FilenameOffset);
	FilenameBuffer[FilenameOffset] = '\0';

	return FilenameBuffer;
}

char *BuildPathAndFilenameFromPathAndFilename(char *pPath, char *pFilename)
{
	int StringLen = strlen(pPath) + strlen(pFilename) + 1;
	BOOL SeperatingSlash = TRUE;
	char *PathAndFilename;

	if (pPath[strlen(pPath)-1]!=DIRECTORY_SEPERATOR)
	{
		StringLen++;
		SeperatingSlash = FALSE;
	}

	PathAndFilename = malloc(StringLen);

	if (PathAndFilename!=NULL)
	{
		memcpy(PathAndFilename, pPath,strlen(pPath)+1);
		
		if (SeperatingSlash==FALSE)
		{
			PathAndFilename[strlen(pPath)] = DIRECTORY_SEPERATOR;
			PathAndFilename[strlen(pPath)+1] = '\0';
		}

		strcat(PathAndFilename, pFilename);
	}

	return PathAndFilename;
}



