/*
 * Cursor and icon support
 *
 * Copyright 1995 Alexandre Julliard
 *           1996 Martin Von Loewis
 *           1997 Alex Korobka
 *           1998 Turchanov Sergey
 */

/*
 * Theory:
 *
 * http://www.microsoft.com/win32dev/ui/icons.htm
 *
 * Cursors and icons are stored in a global heap block, with the
 * following layout:
 *
 * CURSORICONINFO info;
 * BYTE[]         ANDbits;
 * BYTE[]         XORbits;
 *
 * The bits structures are in the format of a device-dependent bitmap.
 *
 * This layout is very sub-optimal, as the bitmap bits are stored in
 * the X client instead of in the server like other bitmaps; however,
 * some programs (notably Paint Brush) expect to be able to manipulate
 * the bits directly :-(
 *
 * FIXME: what are we going to do with animation and color (bpp > 1) cursors ?!
 */

#include <string.h>
#include <stdlib.h>
#include "heap.h"
#include "windows.h"
#include "peexe.h"
#include "color.h"
#include "bitmap.h"
#include "cursoricon.h"
#include "dc.h"
#include "gdi.h"
#include "sysmetrics.h"
#include "global.h"
#include "module.h"
#include "win.h"
#include "debug.h"
#include "task.h"
#include "user.h"
#include "input.h"
#include "display.h"
#include "message.h"
#include "winerror.h"

static HCURSOR32 hActiveCursor = 0;  /* Active cursor */
static INT32 CURSOR_ShowCount = 0;   /* Cursor display count */
static RECT32 CURSOR_ClipRect;       /* Cursor clipping rect */

/**********************************************************************
 *	    CURSORICON_FindBestIcon
 *
 * Find the icon closest to the requested size and number of colors.
 */
static ICONDIRENTRY *CURSORICON_FindBestIcon( CURSORICONDIR *dir, int width,
                                              int height, int colors )
{
    int i, maxcolors, maxwidth, maxheight;
    ICONDIRENTRY *entry, *bestEntry = NULL;

    if (dir->idCount < 1)
    {
        WARN(icon, "Empty directory!\n" );
        return NULL;
    }
    if (dir->idCount == 1) return &dir->idEntries[0].icon;  /* No choice... */

    /* First find the exact size with less colors */

    maxcolors = 0;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth == width) && (entry->bHeight == height) &&
            (entry->bColorCount <= colors) && (entry->bColorCount > maxcolors))
        {
            bestEntry = entry;
            maxcolors = entry->bColorCount;
        }
    if (bestEntry) return bestEntry;

    /* First find the exact size with more colors */

    maxcolors = 255;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth == width) && (entry->bHeight == height) &&
            (entry->bColorCount > colors) && (entry->bColorCount <= maxcolors))
        {
            bestEntry = entry;
            maxcolors = entry->bColorCount;
        }
    if (bestEntry) return bestEntry;

    /* Now find a smaller one with less colors */

    maxcolors = maxwidth = maxheight = 0;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth <= width) && (entry->bHeight <= height) &&
            (entry->bWidth >= maxwidth) && (entry->bHeight >= maxheight) &&
            (entry->bColorCount <= colors) && (entry->bColorCount > maxcolors))
        {
            bestEntry = entry;
            maxwidth  = entry->bWidth;
            maxheight = entry->bHeight;
            maxcolors = entry->bColorCount;
        }
    if (bestEntry) return bestEntry;

    /* Now find a smaller one with more colors */

    maxcolors = 255;
    maxwidth = maxheight = 0;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth <= width) && (entry->bHeight <= height) &&
            (entry->bWidth >= maxwidth) && (entry->bHeight >= maxheight) &&
            (entry->bColorCount > colors) && (entry->bColorCount <= maxcolors))
        {
            bestEntry = entry;
            maxwidth  = entry->bWidth;
            maxheight = entry->bHeight;
            maxcolors = entry->bColorCount;
        }
    if (bestEntry) return bestEntry;

    /* Now find a larger one with less colors */

    maxcolors = 0;
    maxwidth = maxheight = 255;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth <= maxwidth) && (entry->bHeight <= maxheight) &&
            (entry->bColorCount <= colors) && (entry->bColorCount > maxcolors))
        {
            bestEntry = entry;
            maxwidth  = entry->bWidth;
            maxheight = entry->bHeight;
            maxcolors = entry->bColorCount;
        }
    if (bestEntry) return bestEntry;

    /* Now find a larger one with more colors */

    maxcolors = maxwidth = maxheight = 255;
    for (i = 0, entry = &dir->idEntries[0].icon; i < dir->idCount; i++,entry++)
        if ((entry->bWidth <= maxwidth) && (entry->bHeight <= maxheight) &&
            (entry->bColorCount > colors) && (entry->bColorCount <= maxcolors))
        {
            bestEntry = entry;
            maxwidth  = entry->bWidth;
            maxheight = entry->bHeight;
            maxcolors = entry->bColorCount;
        }

    return bestEntry;
}


/**********************************************************************
 *	    CURSORICON_FindBestCursor
 *
 * Find the cursor closest to the requested size.
 * FIXME: parameter 'color' ignored and entries with more than 1 bpp
 *        ignored too
 */
static CURSORDIRENTRY *CURSORICON_FindBestCursor( CURSORICONDIR *dir,
                                                  int width, int height, int color)
{
    int i, maxwidth, maxheight;
    CURSORDIRENTRY *entry, *bestEntry = NULL;

    if (dir->idCount < 1)
    {
        WARN(cursor, "Empty directory!\n" );
        return NULL;
    }
    if (dir->idCount == 1) return &dir->idEntries[0].cursor; /* No choice... */

    /* First find the largest one smaller than or equal to the requested size*/

    maxwidth = maxheight = 0;
    for(i = 0,entry = &dir->idEntries[0].cursor; i < dir->idCount; i++,entry++)
        if ((entry->wWidth <= width) && (entry->wHeight <= height) &&
            (entry->wWidth > maxwidth) && (entry->wHeight > maxheight) &&
	    (entry->wBitCount == 1))
        {
            bestEntry = entry;
            maxwidth  = entry->wWidth;
            maxheight = entry->wHeight;
        }
    if (bestEntry) return bestEntry;

    /* Now find the smallest one larger than the requested size */

    maxwidth = maxheight = 255;
    for(i = 0,entry = &dir->idEntries[0].cursor; i < dir->idCount; i++,entry++)
        if ((entry->wWidth < maxwidth) && (entry->wHeight < maxheight) &&
	    (entry->wBitCount == 1))
        {
            bestEntry = entry;
            maxwidth  = entry->wWidth;
            maxheight = entry->wHeight;
        }

    return bestEntry;
}

/*********************************************************************
 * The main purpose of this function is to create fake resource directory
 * and fake resource entries. There are several reasons for this:
 *	-	CURSORICONDIR and CURSORICONFILEDIR differ in sizes and their
 *              fields
 *	There are some "bad" cursor files which do not have
 *		bColorCount initialized but instead one must read this info
 *		directly from corresponding DIB sections
 * Note: wResId is index to array of pointer returned in ptrs (origin is 1)
 */
BOOL32 CURSORICON_SimulateLoadingFromResourceW( LPWSTR filename, BOOL32 fCursor,
                                                CURSORICONDIR **res, LPBYTE **ptr)
{
    LPBYTE   _free;
    CURSORICONFILEDIR *bits;
    int	     entries, size, i;

    *res = NULL;
    *ptr = NULL;
    if (!(bits = (CURSORICONFILEDIR *)VIRTUAL_MapFileW( filename ))) return FALSE;

    /* FIXME: test for inimated icons
     * hack to load the first icon from the *.ani file
     */
    if ( *(LPDWORD)bits==0x46464952 ) /* "RIFF" */
    { LPBYTE pos = (LPBYTE) bits;
      FIXME (cursor,"Animated icons not correctly implemented! %p \n", bits);
	
      for (;;)
      { if (*(LPDWORD)pos==0x6e6f6369)		/* "icon" */
        { FIXME (cursor,"icon entry found! %p\n", bits);
	  pos+=4;
	  if ( !*(LPWORD) pos==0x2fe)		/* iconsize */
	  { goto fail;
	  }
	  bits+=2;
	  FIXME (cursor,"icon size ok %p \n", bits);
	  break;
	}
        pos+=2;
        if (pos>=(LPBYTE)bits+766) goto fail;
      }
    }
    if (!(entries = bits->idCount)) goto fail;
    (int)_free = size = sizeof(CURSORICONDIR) + sizeof(CURSORICONDIRENTRY) * 
                                                (entries - 1);
    for (i=0; i < entries; i++)
      size += bits->idEntries[i].dwDIBSize + (fCursor ? sizeof(POINT16): 0);
    
    if (!(*ptr = HeapAlloc( GetProcessHeap(), 0,
                            entries * sizeof (CURSORICONDIRENTRY*)))) goto fail;
    if (!(*res = HeapAlloc( GetProcessHeap(), 0, size))) goto fail;

    _free = (LPBYTE)(*res) + (int)_free;
    memcpy((*res), bits, 6);
    for (i=0; i<entries; i++)
    {
      ((LPBYTE*)(*ptr))[i] = _free;
      if (fCursor) {
        (*res)->idEntries[i].cursor.wWidth=bits->idEntries[i].bWidth;
        (*res)->idEntries[i].cursor.wHeight=bits->idEntries[i].bHeight;
        (*res)->idEntries[i].cursor.wPlanes=1;
        (*res)->idEntries[i].cursor.wBitCount = ((LPBITMAPINFOHEADER)((LPBYTE)bits +
                                                   bits->idEntries[i].dwDIBOffset))->biBitCount;
        (*res)->idEntries[i].cursor.dwBytesInRes = bits->idEntries[i].dwDIBSize;
        (*res)->idEntries[i].cursor.wResId=i+1;
	((LPPOINT16)_free)->x=bits->idEntries[i].xHotspot;
	((LPPOINT16)_free)->y=bits->idEntries[i].yHotspot;
	_free+=sizeof(POINT16);
      } else {
        (*res)->idEntries[i].icon.bWidth=bits->idEntries[i].bWidth;
        (*res)->idEntries[i].icon.bHeight=bits->idEntries[i].bHeight;
        (*res)->idEntries[i].icon.bColorCount = bits->idEntries[i].bColorCount;
        (*res)->idEntries[i].icon.wPlanes=1;
	(*res)->idEntries[i].icon.wBitCount= ((LPBITMAPINFOHEADER)((LPBYTE)bits +
                                             bits->idEntries[i].dwDIBOffset))->biBitCount;
        (*res)->idEntries[i].icon.dwBytesInRes = bits->idEntries[i].dwDIBSize;
        (*res)->idEntries[i].icon.wResId=i+1;
      }
      memcpy(_free,(LPBYTE)bits +bits->idEntries[i].dwDIBOffset,
             (*res)->idEntries[i].icon.dwBytesInRes);
      _free += (*res)->idEntries[i].icon.dwBytesInRes;
    }
    UnmapViewOfFile( bits );
    return TRUE;    
fail:
    if (*res) HeapFree( GetProcessHeap(), 0, *res );
    if (*ptr) HeapFree( GetProcessHeap(), 0, *ptr );
    UnmapViewOfFile( bits );
    return FALSE;
}

/**********************************************************************
 *	    CURSORICON_LoadDirEntry16
 *
 * Load the icon/cursor directory for a given resource name and find the
 * best matching entry.
 */
static BOOL32 CURSORICON_LoadDirEntry16( HINSTANCE32 hInstance, SEGPTR name,
                                         INT32 width, INT32 height, INT32 colors, 
					 BOOL32 fCursor, CURSORICONDIRENTRY *dirEntry )
{
    HRSRC16 hRsrc;
    HGLOBAL16 hMem;
    CURSORICONDIR *dir;
    CURSORICONDIRENTRY *entry = NULL;

    if (!(hRsrc = FindResource16( hInstance, name,
                              fCursor ? RT_GROUP_CURSOR16 : RT_GROUP_ICON16 )))
        return FALSE;
    if (!(hMem = LoadResource16( hInstance, hRsrc ))) return FALSE;
    if ((dir = (CURSORICONDIR *)LockResource16( hMem )))
    {
        if (fCursor)
            entry = (CURSORICONDIRENTRY *)CURSORICON_FindBestCursor( dir,
                                                               width, height, 1);
        else
            entry = (CURSORICONDIRENTRY *)CURSORICON_FindBestIcon( dir,
                                                       width, height, colors );
        if (entry) *dirEntry = *entry;
    }
    FreeResource16( hMem );
    return (entry != NULL);
}


/**********************************************************************
 *          CURSORICON_LoadDirEntry32
 *
 * Load the icon/cursor directory for a given resource name and find the
 * best matching entry.
 */
static BOOL32 CURSORICON_LoadDirEntry32( HINSTANCE32 hInstance, LPCWSTR name,
                                         INT32 width, INT32 height, INT32 colors,
                                         BOOL32 fCursor, CURSORICONDIRENTRY *dirEntry )
{
    HANDLE32 hRsrc;
    HANDLE32 hMem;
    CURSORICONDIR *dir;
    CURSORICONDIRENTRY *entry = NULL;

    if (!(hRsrc = FindResource32W( hInstance, name,
                            fCursor ? RT_GROUP_CURSOR32W : RT_GROUP_ICON32W )))
        return FALSE;
    if (!(hMem = LoadResource32( hInstance, hRsrc ))) return FALSE;
    if ((dir = (CURSORICONDIR*)LockResource32( hMem )))
    {
        if (fCursor)
            entry = (CURSORICONDIRENTRY *)CURSORICON_FindBestCursor( dir,
                                                               width, height, 1);
        else
            entry = (CURSORICONDIRENTRY *)CURSORICON_FindBestIcon( dir,
                                                       width, height, colors );
        if (entry) *dirEntry = *entry;
    }
    FreeResource32( hMem );
    return (entry != NULL);
}


/**********************************************************************
 *	    CURSORICON_CreateFromResource
 *
 * Create a cursor or icon from in-memory resource template. 
 *
 * FIXME: Convert to mono when cFlag is LR_MONOCHROME. Do something
 *        with cbSize parameter as well.
 */
static HGLOBAL16 CURSORICON_CreateFromResource( HINSTANCE16 hInstance, HGLOBAL16 hObj, LPBYTE bits,
	 					UINT32 cbSize, BOOL32 bIcon, DWORD dwVersion, 
						INT32 width, INT32 height, UINT32 loadflags )
{
    int sizeAnd, sizeXor;
    HBITMAP32 hAndBits = 0, hXorBits = 0; /* error condition for later */
    BITMAPOBJ *bmpXor, *bmpAnd;
    POINT16 hotspot = { 0 ,0 };
    BITMAPINFO *bmi;
    HDC32 hdc;
    BOOL32 DoStretch;
    INT32 size;

    TRACE(cursor,"%08x (%u bytes), ver %08x, %ix%i %s %s\n",
                        (unsigned)bits, cbSize, (unsigned)dwVersion, width, height,
                                  bIcon ? "icon" : "cursor", (loadflags & LR_MONOCHROME) ? "mono" : "" );
    if (dwVersion == 0x00020000)
    {
	FIXME(cursor,"\t2.xx resources are not supported\n");
	return 0;
    }

    if (bIcon)
	bmi = (BITMAPINFO *)bits;
    else /* get the hotspot */
    {
        POINT16 *pt = (POINT16 *)bits;
        hotspot = *pt;
        bmi = (BITMAPINFO *)(pt + 1);
    }
    size = DIB_BitmapInfoSize( bmi, DIB_RGB_COLORS );

    if (!width) width = bmi->bmiHeader.biWidth;
    if (!height) height = bmi->bmiHeader.biHeight/2;
    DoStretch = (bmi->bmiHeader.biHeight/2 != height) ||
      (bmi->bmiHeader.biWidth != width);

    /* Check bitmap header */

    if ( (bmi->bmiHeader.biSize != sizeof(BITMAPCOREHEADER)) &&
	 (bmi->bmiHeader.biSize != sizeof(BITMAPINFOHEADER)  ||
	  bmi->bmiHeader.biCompression != BI_RGB) )
    {
          WARN(cursor,"\tinvalid resource bitmap header.\n");
          return 0;
    }

    if( (hdc = GetDC32( 0 )) )
    {
	BITMAPINFO* pInfo;

        /* Make sure we have room for the monochrome bitmap later on.
	 * Note that BITMAPINFOINFO and BITMAPCOREHEADER are the same
	 * up to and including the biBitCount. In-memory icon resource 
	 * format is as follows:
	 *
	 *   BITMAPINFOHEADER   icHeader  // DIB header
	 *   RGBQUAD         icColors[]   // Color table
  	 *   BYTE            icXOR[]      // DIB bits for XOR mask
	 *   BYTE            icAND[]      // DIB bits for AND mask
	 */

    	if ((pInfo = (BITMAPINFO *)HeapAlloc( GetProcessHeap(), 0, 
	  MAX(size, sizeof(BITMAPINFOHEADER) + 2*sizeof(RGBQUAD)))))
	{	
	    memcpy( pInfo, bmi, size );	
	    pInfo->bmiHeader.biHeight /= 2;

	    /* Create the XOR bitmap */

	    if (DoStretch) {
	      if ((hXorBits = CreateCompatibleBitmap32(hdc, width, height))) {
		HBITMAP32 hOld;
		HDC32 hMem = CreateCompatibleDC32(hdc);
		BOOL32 res;

		if (hMem) {
		  hOld = SelectObject32(hMem, hXorBits);
	          res = StretchDIBits32(hMem, 0, 0, width, height, 0, 0,
		    bmi->bmiHeader.biWidth, bmi->bmiHeader.biHeight/2,
		    (char*)bmi + size, pInfo, DIB_RGB_COLORS, SRCCOPY);
		  SelectObject32(hMem, hOld);
		  DeleteDC32(hMem);
	        } else res = FALSE;
		if (!res) { DeleteObject32(hXorBits); hXorBits = 0; }
	      }
	    } else hXorBits = CreateDIBitmap32( hdc, &pInfo->bmiHeader,
		CBM_INIT, (char*)bmi + size, pInfo, DIB_RGB_COLORS );
	    if( hXorBits )
	    {
		char* bits = (char *)bmi + size + bmi->bmiHeader.biHeight *
				DIB_GetDIBWidthBytes(bmi->bmiHeader.biWidth,
						     bmi->bmiHeader.biBitCount) / 2;

		pInfo->bmiHeader.biBitCount = 1;
	        if (pInfo->bmiHeader.biSize == sizeof(BITMAPINFOHEADER))
	        {
	            RGBQUAD *rgb = pInfo->bmiColors;

	            pInfo->bmiHeader.biClrUsed = pInfo->bmiHeader.biClrImportant = 2;
	            rgb[0].rgbBlue = rgb[0].rgbGreen = rgb[0].rgbRed = 0x00;
	            rgb[1].rgbBlue = rgb[1].rgbGreen = rgb[1].rgbRed = 0xff;
	            rgb[0].rgbReserved = rgb[1].rgbReserved = 0;
	        }
	        else
	        {
	            RGBTRIPLE *rgb = (RGBTRIPLE *)(((BITMAPCOREHEADER *)pInfo) + 1);

	            rgb[0].rgbtBlue = rgb[0].rgbtGreen = rgb[0].rgbtRed = 0x00;
	            rgb[1].rgbtBlue = rgb[1].rgbtGreen = rgb[1].rgbtRed = 0xff;
	        }

	        /* Create the AND bitmap */

	    if (DoStretch) {
	      if ((hAndBits = CreateBitmap32(width, height, 1, 1, NULL))) {
		HBITMAP32 hOld;
		HDC32 hMem = CreateCompatibleDC32(hdc);
		BOOL32 res;

		if (hMem) {
		  hOld = SelectObject32(hMem, hAndBits);
	          res = StretchDIBits32(hMem, 0, 0, width, height, 0, 0,
		    pInfo->bmiHeader.biWidth, pInfo->bmiHeader.biHeight,
		    bits, pInfo, DIB_RGB_COLORS, SRCCOPY);
		  SelectObject32(hMem, hOld);
		  DeleteDC32(hMem);
	        } else res = FALSE;
		if (!res) { DeleteObject32(hAndBits); hAndBits = 0; }
	      }
	    } else hAndBits = CreateDIBitmap32( hdc, &pInfo->bmiHeader,
	      CBM_INIT, bits, pInfo, DIB_RGB_COLORS );

		if( !hAndBits ) DeleteObject32( hXorBits );
	    }
	    HeapFree( GetProcessHeap(), 0, pInfo ); 
	}
	ReleaseDC32( 0, hdc );
    }

    if( !hXorBits || !hAndBits ) 
    {
	WARN(cursor,"\tunable to create an icon bitmap.\n");
	return 0;
    }

    /* Now create the CURSORICONINFO structure */
    bmpXor = (BITMAPOBJ *) GDI_GetObjPtr( hXorBits, BITMAP_MAGIC );
    bmpAnd = (BITMAPOBJ *) GDI_GetObjPtr( hAndBits, BITMAP_MAGIC );
    sizeXor = bmpXor->bitmap.bmHeight * bmpXor->bitmap.bmWidthBytes;
    sizeAnd = bmpAnd->bitmap.bmHeight * bmpAnd->bitmap.bmWidthBytes;

    if (hObj) hObj = GlobalReAlloc16( hObj, 
		     sizeof(CURSORICONINFO) + sizeXor + sizeAnd, GMEM_MOVEABLE );
    if (!hObj) hObj = GlobalAlloc16( GMEM_MOVEABLE, 
		     sizeof(CURSORICONINFO) + sizeXor + sizeAnd );
    if (hObj)
    {
	CURSORICONINFO *info;

	/* Make it owned by the module */
	if (hInstance) FarSetOwner( hObj, GetExePtr(hInstance) );

	info = (CURSORICONINFO *)GlobalLock16( hObj );
	info->ptHotSpot.x   = hotspot.x;
	info->ptHotSpot.y   = hotspot.y;
	info->nWidth        = bmpXor->bitmap.bmWidth;
	info->nHeight       = bmpXor->bitmap.bmHeight;
	info->nWidthBytes   = bmpXor->bitmap.bmWidthBytes;
	info->bPlanes       = bmpXor->bitmap.bmPlanes;
	info->bBitsPerPixel = bmpXor->bitmap.bmBitsPixel;

	/* Transfer the bitmap bits to the CURSORICONINFO structure */

	GetBitmapBits32( hAndBits, sizeAnd, (char *)(info + 1) );
	GetBitmapBits32( hXorBits, sizeXor, (char *)(info + 1) + sizeAnd );
	GlobalUnlock16( hObj );
    }

    DeleteObject32( hXorBits );
    DeleteObject32( hAndBits );
    return hObj;
}


/**********************************************************************
 *          CreateIconFromResourceEx16          (USER.450)
 *
 * FIXME: not sure about exact parameter types
 */
HICON16 WINAPI CreateIconFromResourceEx16( LPBYTE bits, UINT16 cbSize, BOOL16 bIcon,
                                    DWORD dwVersion, INT16 width, INT16 height, UINT16 cFlag )
{
    return CreateIconFromResourceEx32(bits, cbSize, bIcon, dwVersion, 
      width, height, cFlag);
}


/**********************************************************************
 *          CreateIconFromResource          (USER32.76)
 */
HICON32 WINAPI CreateIconFromResource32( LPBYTE bits, UINT32 cbSize,
                                           BOOL32 bIcon, DWORD dwVersion)
{
    return CreateIconFromResourceEx32( bits, cbSize, bIcon, dwVersion, 0,0,0);
}


/**********************************************************************
 *          CreateIconFromResourceEx32          (USER32.77)
 */
HICON32 WINAPI CreateIconFromResourceEx32( LPBYTE bits, UINT32 cbSize,
                                           BOOL32 bIcon, DWORD dwVersion,
                                           INT32 width, INT32 height,
                                           UINT32 cFlag )
{
    TDB* pTask = (TDB*)GlobalLock16( GetCurrentTask() );
    if( pTask )
	return CURSORICON_CreateFromResource( pTask->hInstance, 0, bits, cbSize, bIcon, dwVersion,
					      width, height, cFlag );
    return 0;
}


/**********************************************************************
 *	    CURSORICON_Load16
 *
 * Load a cursor or icon from a 16-bit resource.
 */
static HGLOBAL16 CURSORICON_Load16( HINSTANCE16 hInstance, SEGPTR name,
                                    INT32 width, INT32 height, INT32 colors,
                                    BOOL32 fCursor, UINT32 loadflags)
{
    HGLOBAL16 handle = 0;
    HRSRC16 hRsrc;
    CURSORICONDIRENTRY dirEntry;

    if (!hInstance)  /* OEM cursor/icon */
    {
        HDC32 hdc;
	DC *dc;

        if (HIWORD(name))  /* Check for '#xxx' name */
        {
            char *ptr = PTR_SEG_TO_LIN( name );
            if (ptr[0] != '#') return 0;
            if (!(name = (SEGPTR)atoi( ptr + 1 ))) return 0;
        }
	hdc = CreateDC32A( "DISPLAY", NULL, NULL, NULL );
	dc = DC_GetDCPtr( hdc );
	if(dc->funcs->pLoadOEMResource)
	    handle = dc->funcs->pLoadOEMResource( LOWORD(name), fCursor ?
						  OEM_CURSOR : OEM_ICON);
	GDI_HEAP_UNLOCK( hdc );
	DeleteDC32( hdc );
	return handle;
    }

    /* Find the best entry in the directory */

    if ( !CURSORICON_LoadDirEntry16( hInstance, name, width, height,
                                    colors, fCursor, &dirEntry ) )  return 0;
    /* Load the resource */

    if ( (hRsrc = FindResource16( hInstance,
                                MAKEINTRESOURCE16( dirEntry.icon.wResId ),
                                fCursor ? RT_CURSOR16 : RT_ICON16 )) )
    {
	/* 16-bit icon or cursor resources are processed
	 * transparently by the LoadResource16() via custom
	 * resource handlers set by SetResourceHandler().
	 */

	if ( (handle = LoadResource16( hInstance, hRsrc )) )
	    return handle;
    }
    return 0;
}

/**********************************************************************
 *          CURSORICON_Load32
 *
 * Load a cursor or icon from a 32-bit resource.
 */
HGLOBAL32 CURSORICON_Load32( HINSTANCE32 hInstance, LPCWSTR name,
                             int width, int height, int colors,
                             BOOL32 fCursor, UINT32 loadflags )
{
    HANDLE32 handle = 0, h = 0;
    HANDLE32 hRsrc;
    CURSORICONDIRENTRY dirEntry;
    LPBYTE bits;

    if (!(loadflags & LR_LOADFROMFILE))
    {
        if (!hInstance)  /* OEM cursor/icon */
        {
            WORD resid;
	    HDC32 hdc;
	    DC *dc;

            if(HIWORD(name))
            {
                LPSTR ansi = HEAP_strdupWtoA(GetProcessHeap(),0,name);
                if( ansi[0]=='#')        /*Check for '#xxx' name */
                {
                    resid = atoi(ansi+1);
                    HeapFree( GetProcessHeap(), 0, ansi );
                }
                else
                {
                    HeapFree( GetProcessHeap(), 0, ansi );
                    return 0;
                }
            }
            else resid = LOWORD(name);
	    hdc = CreateDC32A( "DISPLAY", NULL, NULL, NULL );
	    dc = DC_GetDCPtr( hdc );
	    if(dc->funcs->pLoadOEMResource)
	        handle = dc->funcs->pLoadOEMResource( resid, fCursor ?
						      OEM_CURSOR : OEM_ICON );
	    GDI_HEAP_UNLOCK( hdc );
	    DeleteDC32(  hdc );
            return handle;
        }

        /* Find the best entry in the directory */
 
        if (!CURSORICON_LoadDirEntry32( hInstance, name, width, height,
                                        colors, fCursor, &dirEntry ) )  return 0;
        /* Load the resource */

        if (!(hRsrc = FindResource32W(hInstance,MAKEINTRESOURCE32W(dirEntry.icon.wResId),
                                      fCursor ? RT_CURSOR32W : RT_ICON32W ))) return 0;
        if (!(handle = LoadResource32( hInstance, hRsrc ))) return 0;
        /* Hack to keep LoadCursor/Icon32() from spawning multiple
         * copies of the same object.
         */
#define pRsrcEntry ((PIMAGE_RESOURCE_DATA_ENTRY)hRsrc)
        if( pRsrcEntry->ResourceHandle ) return pRsrcEntry->ResourceHandle;
        bits = (LPBYTE)LockResource32( handle );
        h = CURSORICON_CreateFromResource( 0, 0, bits, dirEntry.icon.dwBytesInRes, 
                                           !fCursor, 0x00030000, width, height, loadflags);
        pRsrcEntry->ResourceHandle = h;
    }
    else
    {
        CURSORICONDIR *res;
        LPBYTE *ptr;
        if (!CURSORICON_SimulateLoadingFromResourceW((LPWSTR)name, fCursor, &res, &ptr))
            return 0;
        if (fCursor)
            dirEntry = *(CURSORICONDIRENTRY *)CURSORICON_FindBestCursor(res, width, height, 1);
        else
            dirEntry = *(CURSORICONDIRENTRY *)CURSORICON_FindBestIcon(res, width, height, colors);
        bits = ptr[dirEntry.icon.wResId-1];
        h = CURSORICON_CreateFromResource( 0, 0, bits, dirEntry.icon.dwBytesInRes, 
                                           !fCursor, 0x00030000, width, height, loadflags);
        HeapFree( GetProcessHeap(), 0, res );
        HeapFree( GetProcessHeap(), 0, ptr );
    }
    return h;
#undef  pRsrcEntry
}


/***********************************************************************
 *           CURSORICON_Copy
 *
 * Make a copy of a cursor or icon.
 */
static HGLOBAL16 CURSORICON_Copy( HINSTANCE16 hInstance, HGLOBAL16 handle )
{
    char *ptrOld, *ptrNew;
    int size;
    HGLOBAL16 hNew;

    if (!(ptrOld = (char *)GlobalLock16( handle ))) return 0;
    if (!(hInstance = GetExePtr( hInstance ))) return 0;
    size = GlobalSize16( handle );
    hNew = GlobalAlloc16( GMEM_MOVEABLE, size );
    FarSetOwner( hNew, hInstance );
    ptrNew = (char *)GlobalLock16( hNew );
    memcpy( ptrNew, ptrOld, size );
    GlobalUnlock16( handle );
    GlobalUnlock16( hNew );
    return hNew;
}

/***********************************************************************
 *           CURSORICON_IconToCursor
 *
 * Converts bitmap to mono and truncates if icon is too large (should
 * probably do StretchBlt() instead).
 */
HCURSOR16 CURSORICON_IconToCursor(HICON16 hIcon, BOOL32 bSemiTransparent)
{
 HCURSOR16       hRet = 0;
 CURSORICONINFO *pIcon = NULL;
 HTASK16 	 hTask = GetCurrentTask();
 TDB*  		 pTask = (TDB *)GlobalLock16(hTask);

 if(hIcon && pTask)
    if (!(pIcon = (CURSORICONINFO*)GlobalLock16( hIcon ))) return FALSE;
       if (pIcon->bPlanes * pIcon->bBitsPerPixel == 1)
           hRet = CURSORICON_Copy( pTask->hInstance, hIcon );
       else
       {
           BYTE  pAndBits[128];
           BYTE  pXorBits[128];
	   int   maxx, maxy, ix, iy, bpp = pIcon->bBitsPerPixel;
           BYTE* psPtr, *pxbPtr = pXorBits;
           unsigned xor_width, and_width, val_base = 0xffffffff >> (32 - bpp);
           BYTE* pbc = NULL;

           COLORREF       col;
           CURSORICONINFO cI;

	   TRACE(icon, "[%04x] %ix%i %ibpp (bogus %ibps)\n", 
		hIcon, pIcon->nWidth, pIcon->nHeight, pIcon->bBitsPerPixel, pIcon->nWidthBytes );

	   xor_width = BITMAP_GetWidthBytes( pIcon->nWidth, bpp );
	   and_width = BITMAP_GetWidthBytes( pIcon->nWidth, 1 );
	   psPtr = (BYTE *)(pIcon + 1) + pIcon->nHeight * and_width;

           memset(pXorBits, 0, 128);
           cI.bBitsPerPixel = 1; cI.bPlanes = 1;
           cI.ptHotSpot.x = cI.ptHotSpot.y = 15;
           cI.nWidth = 32; cI.nHeight = 32;
           cI.nWidthBytes = 4;	/* 32x1bpp */

           maxx = (pIcon->nWidth > 32) ? 32 : pIcon->nWidth;
           maxy = (pIcon->nHeight > 32) ? 32 : pIcon->nHeight;

           for( iy = 0; iy < maxy; iy++ )
           {
	      unsigned shift = iy % 2; 

              memcpy( pAndBits + iy * 4, (BYTE *)(pIcon + 1) + iy * and_width, 
					 (and_width > 4) ? 4 : and_width );
              for( ix = 0; ix < maxx; ix++ )
              {
                if( bSemiTransparent && ((ix+shift)%2) )
                {
		    /* set AND bit, XOR bit stays 0 */

                    pbc = pAndBits + iy * 4 + ix/8;
                   *pbc |= 0x80 >> (ix%8);
                }
                else
                {
		    /* keep AND bit, set XOR bit */

		  unsigned *psc = (unsigned*)(psPtr + (ix * bpp)/8);
                  unsigned  val = ((*psc) >> (ix * bpp)%8) & val_base;
                  col = COLOR_ToLogical(val);
		  if( (GetRValue(col) + GetGValue(col) + GetBValue(col)) > 0x180 )
                  {
                    pbc = pxbPtr + ix/8;
                   *pbc |= 0x80 >> (ix%8);
                  }
                }
              }
              psPtr += xor_width;
              pxbPtr += 4;
           }

           hRet = CreateCursorIconIndirect( pTask->hInstance , &cI, pAndBits, pXorBits);

           if( !hRet ) /* fall back on default drag cursor */
                hRet = CURSORICON_Copy( pTask->hInstance ,
                              CURSORICON_Load16(0,MAKEINTRESOURCE16(OCR_DRAGOBJECT),
                                         SYSMETRICS_CXCURSOR, SYSMETRICS_CYCURSOR, 1, TRUE, 0) );
       }

 return hRet;
}


/***********************************************************************
 *           LoadCursor16    (USER.173)
 */
HCURSOR16 WINAPI LoadCursor16( HINSTANCE16 hInstance, SEGPTR name )
{
    if (HIWORD(name))
        TRACE(cursor, "%04x '%s'\n",
                        hInstance, (char *)PTR_SEG_TO_LIN( name ) );
    else
        TRACE(cursor, "%04x %04x\n",
                        hInstance, LOWORD(name) );

    return CURSORICON_Load16( hInstance, name,
                              SYSMETRICS_CXCURSOR, SYSMETRICS_CYCURSOR, 1, TRUE, 0);
}


/***********************************************************************
 *           LoadIcon16    (USER.174)
 */
HICON16 WINAPI LoadIcon16( HINSTANCE16 hInstance, SEGPTR name )
{
    HDC32 hdc = GetDC32(0);
    UINT32 palEnts = GetSystemPaletteEntries32(hdc, 0, 0, NULL);
    ReleaseDC32(0, hdc);

    if (HIWORD(name))
        TRACE(icon, "%04x '%s'\n",
                      hInstance, (char *)PTR_SEG_TO_LIN( name ) );
    else
        TRACE(icon, "%04x %04x\n",
                      hInstance, LOWORD(name) );

    return CURSORICON_Load16( hInstance, name,
                              SYSMETRICS_CXICON, SYSMETRICS_CYICON,
                              MIN(16, palEnts), FALSE, 0);
}


/***********************************************************************
 *           CreateCursor16    (USER.406)
 */
HCURSOR16 WINAPI CreateCursor16( HINSTANCE16 hInstance,
                                 INT16 xHotSpot, INT16 yHotSpot,
                                 INT16 nWidth, INT16 nHeight,
                                 LPCVOID lpANDbits, LPCVOID lpXORbits )
{
    CURSORICONINFO info = { { xHotSpot, yHotSpot }, nWidth, nHeight, 0, 1, 1 };

    TRACE(cursor, "%dx%d spot=%d,%d xor=%p and=%p\n",
                    nWidth, nHeight, xHotSpot, yHotSpot, lpXORbits, lpANDbits);
    return CreateCursorIconIndirect( hInstance, &info, lpANDbits, lpXORbits );
}


/***********************************************************************
 *           CreateCursor32    (USER32.67)
 */
HCURSOR32 WINAPI CreateCursor32( HINSTANCE32 hInstance,
                                 INT32 xHotSpot, INT32 yHotSpot,
                                 INT32 nWidth, INT32 nHeight,
                                 LPCVOID lpANDbits, LPCVOID lpXORbits )
{
    CURSORICONINFO info = { { xHotSpot, yHotSpot }, nWidth, nHeight, 0, 1, 1 };

    TRACE(cursor, "%dx%d spot=%d,%d xor=%p and=%p\n",
                    nWidth, nHeight, xHotSpot, yHotSpot, lpXORbits, lpANDbits);
    return CreateCursorIconIndirect( 0, &info, lpANDbits, lpXORbits );
}


/***********************************************************************
 *           CreateIcon16    (USER.407)
 */
HICON16 WINAPI CreateIcon16( HINSTANCE16 hInstance, INT16 nWidth,
                             INT16 nHeight, BYTE bPlanes, BYTE bBitsPixel,
                             LPCVOID lpANDbits, LPCVOID lpXORbits )
{
    CURSORICONINFO info = { { 0, 0 }, nWidth, nHeight, 0, bPlanes, bBitsPixel};

    TRACE(icon, "%dx%dx%d, xor=%p, and=%p\n",
                  nWidth, nHeight, bPlanes * bBitsPixel, lpXORbits, lpANDbits);
    return CreateCursorIconIndirect( hInstance, &info, lpANDbits, lpXORbits );
}


/***********************************************************************
 *           CreateIcon32    (USER32.75)
 */
HICON32 WINAPI CreateIcon32( HINSTANCE32 hInstance, INT32 nWidth,
                             INT32 nHeight, BYTE bPlanes, BYTE bBitsPixel,
                             LPCVOID lpANDbits, LPCVOID lpXORbits )
{
    CURSORICONINFO info = { { 0, 0 }, nWidth, nHeight, 0, bPlanes, bBitsPixel};

    TRACE(icon, "%dx%dx%d, xor=%p, and=%p\n",
                  nWidth, nHeight, bPlanes * bBitsPixel, lpXORbits, lpANDbits);
    return CreateCursorIconIndirect( 0, &info, lpANDbits, lpXORbits );
}


/***********************************************************************
 *           CreateCursorIconIndirect    (USER.408)
 */
HGLOBAL16 WINAPI CreateCursorIconIndirect( HINSTANCE16 hInstance,
                                           CURSORICONINFO *info,
                                           LPCVOID lpANDbits,
                                           LPCVOID lpXORbits )
{
    HGLOBAL16 handle;
    char *ptr;
    int sizeAnd, sizeXor;

    hInstance = GetExePtr( hInstance );  /* Make it a module handle */
    if (!lpXORbits || !lpANDbits || info->bPlanes != 1) return 0;
    info->nWidthBytes = BITMAP_GetWidthBytes(info->nWidth,info->bBitsPerPixel);
    sizeXor = info->nHeight * info->nWidthBytes;
    sizeAnd = info->nHeight * BITMAP_GetWidthBytes( info->nWidth, 1 );
    if (!(handle = GlobalAlloc16( GMEM_MOVEABLE,
                                  sizeof(CURSORICONINFO) + sizeXor + sizeAnd)))
        return 0;
    if (hInstance) FarSetOwner( handle, hInstance );
    ptr = (char *)GlobalLock16( handle );
    memcpy( ptr, info, sizeof(*info) );
    memcpy( ptr + sizeof(CURSORICONINFO), lpANDbits, sizeAnd );
    memcpy( ptr + sizeof(CURSORICONINFO) + sizeAnd, lpXORbits, sizeXor );
    GlobalUnlock16( handle );
    return handle;
}


/***********************************************************************
 *           CopyIcon16    (USER.368)
 */
HICON16 WINAPI CopyIcon16( HINSTANCE16 hInstance, HICON16 hIcon )
{
    TRACE(icon, "%04x %04x\n", hInstance, hIcon );
    return CURSORICON_Copy( hInstance, hIcon );
}


/***********************************************************************
 *           CopyIcon32    (USER32.60)
 */
HICON32 WINAPI CopyIcon32( HICON32 hIcon )
{
  HTASK16 hTask = GetCurrentTask ();
  TDB* pTask = (TDB *) GlobalLock16 (hTask);
    TRACE(icon, "%04x\n", hIcon );
  return CURSORICON_Copy( pTask->hInstance, hIcon );
}


/***********************************************************************
 *           CopyCursor16    (USER.369)
 */
HCURSOR16 WINAPI CopyCursor16( HINSTANCE16 hInstance, HCURSOR16 hCursor )
{
    TRACE(cursor, "%04x %04x\n", hInstance, hCursor );
    return CURSORICON_Copy( hInstance, hCursor );
}


/***********************************************************************
 *           DestroyIcon16    (USER.457)
 */
BOOL16 WINAPI DestroyIcon16( HICON16 hIcon )
{
    TRACE(icon, "%04x\n", hIcon );
    /* FIXME: should check for OEM/global heap icon here */
    return (FreeResource16( hIcon ) == 0);
}


/***********************************************************************
 *           DestroyIcon32    (USER32.133)
 */
BOOL32 WINAPI DestroyIcon32( HICON32 hIcon )
{
    TRACE(icon, "%04x\n", hIcon );
    /* FIXME: should check for OEM/global heap icon here */
    /* Unlike DestroyIcon16, only icons created with CreateIcon32
       are valid for DestroyIcon32, so don't use FreeResource32 */
    return (GlobalFree16( hIcon ) == 0);
}


/***********************************************************************
 *           DestroyCursor16    (USER.458)
 */
BOOL16 WINAPI DestroyCursor16( HCURSOR16 hCursor )
{
    TRACE(cursor, "%04x\n", hCursor );
    if (FreeResource16( hCursor ) == 0)
      return TRUE;
    else
      /* I believe this very same line should be added for every function
	 where appears the comment:

	 "FIXME: should check for OEM/global heap cursor here"

	 which are most (all?) the ones that call FreeResource, at least
	 in this module. Maybe this should go to a wrapper to avoid
	 repetition. Or: couldn't it go to FreeResoutce itself?
	 
	 I'll let this to someone savvy on the subject.
	 */
      return (GlobalFree16 (hCursor) == 0);
}


/***********************************************************************
 *           DestroyCursor32    (USER32.132)
 */
BOOL32 WINAPI DestroyCursor32( HCURSOR32 hCursor )
{
    TRACE(cursor, "%04x\n", hCursor );
    /* FIXME: should check for OEM/global heap cursor here */
    /* Unlike DestroyCursor16, only cursors created with CreateCursor32
       are valid for DestroyCursor32, so don't use FreeResource32 */
    return (GlobalFree16( hCursor ) == 0);
}


/***********************************************************************
 *           DrawIcon16    (USER.84)
 */
BOOL16 WINAPI DrawIcon16( HDC16 hdc, INT16 x, INT16 y, HICON16 hIcon )
{
    return DrawIcon32( hdc, x, y, hIcon );
}


/***********************************************************************
 *           DrawIcon32    (USER32.159)
 */
BOOL32 WINAPI DrawIcon32( HDC32 hdc, INT32 x, INT32 y, HICON32 hIcon )
{
    CURSORICONINFO *ptr;
    HDC32 hMemDC;
    HBITMAP32 hXorBits, hAndBits;
    COLORREF oldFg, oldBg;

    if (!(ptr = (CURSORICONINFO *)GlobalLock16( hIcon ))) return FALSE;
    if (!(hMemDC = CreateCompatibleDC32( hdc ))) return FALSE;
    hAndBits = CreateBitmap32( ptr->nWidth, ptr->nHeight, 1, 1,
                               (char *)(ptr+1) );
    hXorBits = CreateBitmap32( ptr->nWidth, ptr->nHeight, ptr->bPlanes,
                               ptr->bBitsPerPixel, (char *)(ptr + 1)
                        + ptr->nHeight * BITMAP_GetWidthBytes(ptr->nWidth,1) );
    oldFg = SetTextColor32( hdc, RGB(0,0,0) );
    oldBg = SetBkColor32( hdc, RGB(255,255,255) );

    if (hXorBits && hAndBits)
    {
        HBITMAP32 hBitTemp = SelectObject32( hMemDC, hAndBits );
        BitBlt32( hdc, x, y, ptr->nWidth, ptr->nHeight, hMemDC, 0, 0, SRCAND );
        SelectObject32( hMemDC, hXorBits );
        BitBlt32(hdc, x, y, ptr->nWidth, ptr->nHeight, hMemDC, 0, 0,SRCINVERT);
        SelectObject32( hMemDC, hBitTemp );
    }
    DeleteDC32( hMemDC );
    if (hXorBits) DeleteObject32( hXorBits );
    if (hAndBits) DeleteObject32( hAndBits );
    GlobalUnlock16( hIcon );
    SetTextColor32( hdc, oldFg );
    SetBkColor32( hdc, oldBg );
    return TRUE;
}


/***********************************************************************
 *           DumpIcon    (USER.459)
 */
DWORD WINAPI DumpIcon( SEGPTR pInfo, WORD *lpLen,
                       SEGPTR *lpXorBits, SEGPTR *lpAndBits )
{
    CURSORICONINFO *info = PTR_SEG_TO_LIN( pInfo );
    int sizeAnd, sizeXor;

    if (!info) return 0;
    sizeXor = info->nHeight * info->nWidthBytes;
    sizeAnd = info->nHeight * BITMAP_GetWidthBytes( info->nWidth, 1 );
    if (lpAndBits) *lpAndBits = pInfo + sizeof(CURSORICONINFO);
    if (lpXorBits) *lpXorBits = pInfo + sizeof(CURSORICONINFO) + sizeAnd;
    if (lpLen) *lpLen = sizeof(CURSORICONINFO) + sizeAnd + sizeXor;
    return MAKELONG( sizeXor, sizeXor );
}


/***********************************************************************
 *           SetCursor16    (USER.69)
 */
HCURSOR16 WINAPI SetCursor16( HCURSOR16 hCursor )
{
    return (HCURSOR16)SetCursor32( hCursor );
}


/***********************************************************************
 *           SetCursor32    (USER32.472)
 * RETURNS:
 *	A handle to the previous cursor shape.
 */
HCURSOR32 WINAPI SetCursor32(
	         HCURSOR32 hCursor /* Handle of cursor to show */
) {
    HCURSOR32 hOldCursor;

    if (hCursor == hActiveCursor) return hActiveCursor;  /* No change */
    TRACE(cursor, "%04x\n", hCursor );
    hOldCursor = hActiveCursor;
    hActiveCursor = hCursor;
    /* Change the cursor shape only if it is visible */
    if (CURSOR_ShowCount >= 0)
    {
        DISPLAY_SetCursor( (CURSORICONINFO*)GlobalLock16( hActiveCursor ) );
        GlobalUnlock16( hActiveCursor );
    }
    return hOldCursor;
}


/***********************************************************************
 *           SetCursorPos16    (USER.70)
 */
void WINAPI SetCursorPos16( INT16 x, INT16 y )
{
    SetCursorPos32( x, y );
}


/***********************************************************************
 *           SetCursorPos32    (USER32.474)
 */
BOOL32 WINAPI SetCursorPos32( INT32 x, INT32 y )
{
    DISPLAY_MoveCursor( x, y );
    return TRUE;
}


/***********************************************************************
 *           ShowCursor16    (USER.71)
 */
INT16 WINAPI ShowCursor16( BOOL16 bShow )
{
    return ShowCursor32( bShow );
}


/***********************************************************************
 *           ShowCursor32    (USER32.530)
 */
INT32 WINAPI ShowCursor32( BOOL32 bShow )
{
    TRACE(cursor, "%d, count=%d\n",
                    bShow, CURSOR_ShowCount );

    if (bShow)
    {
        if (++CURSOR_ShowCount == 0)  /* Show it */
        {
            DISPLAY_SetCursor((CURSORICONINFO*)GlobalLock16( hActiveCursor ));
            GlobalUnlock16( hActiveCursor );
        }
    }
    else
    {
        if (--CURSOR_ShowCount == -1)  /* Hide it */
            DISPLAY_SetCursor( NULL );
    }
    return CURSOR_ShowCount;
}


/***********************************************************************
 *           GetCursor16    (USER.247)
 */
HCURSOR16 WINAPI GetCursor16(void)
{
    return hActiveCursor;
}


/***********************************************************************
 *           GetCursor32    (USER32.227)
 */
HCURSOR32 WINAPI GetCursor32(void)
{
    return hActiveCursor;
}


/***********************************************************************
 *           ClipCursor16    (USER.16)
 */
BOOL16 WINAPI ClipCursor16( const RECT16 *rect )
{
    if (!rect) SetRectEmpty32( &CURSOR_ClipRect );
    else CONV_RECT16TO32( rect, &CURSOR_ClipRect );
    return TRUE;
}


/***********************************************************************
 *           ClipCursor32    (USER32.53)
 */
BOOL32 WINAPI ClipCursor32( const RECT32 *rect )
{
    if (!rect) SetRectEmpty32( &CURSOR_ClipRect );
    else CopyRect32( &CURSOR_ClipRect, rect );
    return TRUE;
}


/***********************************************************************
 *           GetCursorPos16    (USER.17)
 */
BOOL16 WINAPI GetCursorPos16( POINT16 *pt )
{
    DWORD posX, posY, state;

    if (!pt) return 0;
    if (!EVENT_QueryPointer( &posX, &posY, &state ))
	pt->x = pt->y = 0;
    else
    {
	pt->x = posX;
	pt->y = posY;
        if (state & MK_LBUTTON)
            AsyncMouseButtonsStates[0] = MouseButtonsStates[0] = TRUE;
        else
            MouseButtonsStates[0] = FALSE;
        if (state & MK_MBUTTON)
            AsyncMouseButtonsStates[1] = MouseButtonsStates[1] = TRUE;
        else       
            MouseButtonsStates[1] = FALSE;
        if (state & MK_RBUTTON)
            AsyncMouseButtonsStates[2] = MouseButtonsStates[2] = TRUE;
        else
            MouseButtonsStates[2] = FALSE;
    }
    TRACE(cursor, "ret=%d,%d\n", pt->x, pt->y );
    return 1;
}


/***********************************************************************
 *           GetCursorPos32    (USER32.229)
 */
BOOL32 WINAPI GetCursorPos32( POINT32 *pt )
{
    BOOL32 ret;

    POINT16 pt16;
    ret = GetCursorPos16( &pt16 );
    if (pt) CONV_POINT16TO32( &pt16, pt );
    return ((pt) ? ret : 0);
}


/***********************************************************************
 *           GetClipCursor16    (USER.309)
 */
void WINAPI GetClipCursor16( RECT16 *rect )
{
    if (rect) CONV_RECT32TO16( &CURSOR_ClipRect, rect );
}


/***********************************************************************
 *           GetClipCursor32    (USER32.221)
 */
void WINAPI GetClipCursor32( RECT32 *rect )
{
    if (rect) CopyRect32( rect, &CURSOR_ClipRect );
}

/**********************************************************************
 *          LookupIconIdFromDirectoryEx16	(USER.364)
 *
 * FIXME: exact parameter sizes
 */
INT16 WINAPI LookupIconIdFromDirectoryEx16( LPBYTE xdir, BOOL16 bIcon,
	     INT16 width, INT16 height, UINT16 cFlag )
{
    CURSORICONDIR	*dir = (CURSORICONDIR*)xdir;
    UINT16 retVal = 0;
    if( dir && !dir->idReserved && (dir->idType & 3) )
    {
        HDC32 hdc = GetDC32(0);
	UINT32 palEnts = GetSystemPaletteEntries32(hdc, 0, 0, NULL);
	int colors = (cFlag & LR_MONOCHROME) ? 2 : palEnts;
	ReleaseDC32(0, hdc);

	if( bIcon )
	{
	    ICONDIRENTRY* entry;
	    entry = CURSORICON_FindBestIcon( dir, width, height, colors );
	    if( entry ) retVal = entry->wResId;
	}
	else
	{
	    CURSORDIRENTRY* entry;
	    entry = CURSORICON_FindBestCursor( dir, width, height, 1);
	    if( entry ) retVal = entry->wResId;
	}
    }
    else WARN(cursor, "invalid resource directory\n");
    return retVal;
}

/**********************************************************************
 *          LookupIconIdFromDirectoryEx32       (USER32.380)
 */
INT32 WINAPI LookupIconIdFromDirectoryEx32( LPBYTE dir, BOOL32 bIcon,
             INT32 width, INT32 height, UINT32 cFlag )
{
    return LookupIconIdFromDirectoryEx16( dir, bIcon, width, height, cFlag );
}

/**********************************************************************
 *          LookupIconIdFromDirectory		(USER.???)
 */
INT16 WINAPI LookupIconIdFromDirectory16( LPBYTE dir, BOOL16 bIcon )
{
    return LookupIconIdFromDirectoryEx16( dir, bIcon, 
	   bIcon ? SYSMETRICS_CXICON : SYSMETRICS_CXCURSOR,
	   bIcon ? SYSMETRICS_CYICON : SYSMETRICS_CYCURSOR, bIcon ? 0 : LR_MONOCHROME );
}

/**********************************************************************
 *          LookupIconIdFromDirectory		(USER32.379)
 */
INT32 WINAPI LookupIconIdFromDirectory32( LPBYTE dir, BOOL32 bIcon )
{
    return LookupIconIdFromDirectoryEx32( dir, bIcon, 
	   bIcon ? SYSMETRICS_CXICON : SYSMETRICS_CXCURSOR,
	   bIcon ? SYSMETRICS_CYICON : SYSMETRICS_CYCURSOR, bIcon ? 0 : LR_MONOCHROME );
}

/**********************************************************************
 *	    GetIconID    (USER.455)
 */
WORD WINAPI GetIconID( HGLOBAL16 hResource, DWORD resType )
{
    LPBYTE lpDir = (LPBYTE)GlobalLock16(hResource);

    TRACE(cursor, "hRes=%04x, entries=%i\n",
                    hResource, lpDir ? ((CURSORICONDIR*)lpDir)->idCount : 0);

    switch(resType)
    {
	case RT_CURSOR16:
	     return (WORD)LookupIconIdFromDirectoryEx16( lpDir, FALSE, 
			  SYSMETRICS_CXCURSOR, SYSMETRICS_CYCURSOR, LR_MONOCHROME );
	case RT_ICON16:
	     return (WORD)LookupIconIdFromDirectoryEx16( lpDir, TRUE,
			  SYSMETRICS_CXICON, SYSMETRICS_CYICON, 0 );
	default:
	     WARN(cursor, "invalid res type %ld\n", resType );
    }
    return 0;
}

/**********************************************************************
 *          LoadCursorIconHandler    (USER.336)
 *
 * Supposed to load resources of Windows 2.x applications.
 */
HGLOBAL16 WINAPI LoadCursorIconHandler( HGLOBAL16 hResource, HMODULE16 hModule, HRSRC16 hRsrc )
{
    FIXME(cursor,"(%04x,%04x,%04x): old 2.x resources are not supported!\n", 
	  hResource, hModule, hRsrc);
    return (HGLOBAL16)0;
}

/**********************************************************************
 *          LoadDIBIconHandler    (USER.357)
 * 
 * RT_ICON resource loader, installed by USER_SignalProc when module
 * is initialized.
 */
HGLOBAL16 WINAPI LoadDIBIconHandler( HGLOBAL16 hMemObj, HMODULE16 hModule, HRSRC16 hRsrc )
{
    /* If hResource is zero we must allocate a new memory block, if it's
     * non-zero but GlobalLock() returns NULL then it was discarded and
     * we have to recommit some memory, otherwise we just need to check 
     * the block size. See LoadProc() in 16-bit SDK for more.
     */

     hMemObj = USER_CallDefaultRsrcHandler( hMemObj, hModule, hRsrc );
     if( hMemObj )
     {
	 LPBYTE bits = (LPBYTE)GlobalLock16( hMemObj );
	 hMemObj = CURSORICON_CreateFromResource( hModule, hMemObj, bits, 
		   SizeofResource16(hModule, hRsrc), TRUE, 0x00030000, 
		   SYSMETRICS_CXICON, SYSMETRICS_CYICON, LR_DEFAULTCOLOR );
     }
     return hMemObj;
}

/**********************************************************************
 *          LoadDIBCursorHandler    (USER.356)
 *
 * RT_CURSOR resource loader. Same as above.
 */
HGLOBAL16 WINAPI LoadDIBCursorHandler( HGLOBAL16 hMemObj, HMODULE16 hModule, HRSRC16 hRsrc )
{
    hMemObj = USER_CallDefaultRsrcHandler( hMemObj, hModule, hRsrc );
    if( hMemObj )
    {
	LPBYTE bits = (LPBYTE)GlobalLock16( hMemObj );
	hMemObj = CURSORICON_CreateFromResource( hModule, hMemObj, bits,
		  SizeofResource16(hModule, hRsrc), FALSE, 0x00030000,
		  SYSMETRICS_CXCURSOR, SYSMETRICS_CYCURSOR, LR_MONOCHROME );
    }
    return hMemObj;
}

/**********************************************************************
 *	    LoadIconHandler    (USER.456)
 */
HICON16 WINAPI LoadIconHandler( HGLOBAL16 hResource, BOOL16 bNew )
{
    LPBYTE bits = (LPBYTE)LockResource16( hResource );

    TRACE(cursor,"hRes=%04x\n",hResource);

    return CURSORICON_CreateFromResource( 0, 0, bits, 0, TRUE, 
		      bNew ? 0x00030000 : 0x00020000, 0, 0, LR_DEFAULTCOLOR );
}

/***********************************************************************
 *           LoadCursorW                (USER32.362)
 */
HCURSOR32 WINAPI LoadCursor32W(HINSTANCE32 hInstance, LPCWSTR name)
{
    return CURSORICON_Load32( hInstance, name,
                              SYSMETRICS_CXCURSOR, SYSMETRICS_CYCURSOR, 1, TRUE, 0);
}

/***********************************************************************
 *           LoadCursorA                (USER32.359)
 */
HCURSOR32 WINAPI LoadCursor32A(HINSTANCE32 hInstance, LPCSTR name)
{
        HCURSOR32 res=0;
        if(!HIWORD(name))
                return LoadCursor32W(hInstance,(LPCWSTR)name);
        else
        {
            LPWSTR uni = HEAP_strdupAtoW( GetProcessHeap(), 0, name );
            res = LoadCursor32W(hInstance, uni);
            HeapFree( GetProcessHeap(), 0, uni);
        }
        return res;
}
/***********************************************************************
*            LoadCursorFromFile32W    (USER32.361)
*/
HCURSOR32 WINAPI LoadCursorFromFile32W (LPCWSTR name)
{
    return LoadImage32W(0, name, IMAGE_CURSOR, SYSMETRICS_CXCURSOR,
      SYSMETRICS_CYCURSOR, LR_LOADFROMFILE);
}

/***********************************************************************
*            LoadCursorFromFile32A    (USER32.360)
*/
HCURSOR32 WINAPI LoadCursorFromFile32A (LPCSTR name)
{
    HCURSOR32 hcur;
    LPWSTR u_name = HEAP_strdupAtoW( GetProcessHeap(), 0, name );
    hcur = LoadCursorFromFile32W(u_name);
    HeapFree( GetProcessHeap(), 0, u_name );
    return hcur;
}
  
/***********************************************************************
 *           LoadIconW          (USER32.364)
 */
HICON32 WINAPI LoadIcon32W(HINSTANCE32 hInstance, LPCWSTR name)
{
    HDC32 hdc = GetDC32(0);
    UINT32 palEnts = GetSystemPaletteEntries32(hdc, 0, 0, NULL);
    ReleaseDC32(0, hdc);

    return CURSORICON_Load32( hInstance, name,
                              SYSMETRICS_CXICON, SYSMETRICS_CYICON,
                              MIN( 16, palEnts ), FALSE, 0);
}

/***********************************************************************
 *           LoadIconA          (USER32.363)
 */
HICON32 WINAPI LoadIcon32A(HINSTANCE32 hInstance, LPCSTR name)
{
    HICON32 res=0;

    if( !HIWORD(name) )
	return LoadIcon32W(hInstance, (LPCWSTR)name);
    else
    {
	LPWSTR uni = HEAP_strdupAtoW( GetProcessHeap(), 0, name );
	res = LoadIcon32W( hInstance, uni );
	HeapFree( GetProcessHeap(), 0, uni );
    }
    return res;
}

/**********************************************************************
 *          GetIconInfo16       (USER.395)
 */
BOOL16 WINAPI GetIconInfo16(HICON16 hIcon,LPICONINFO16 iconinfo)
{
    ICONINFO32	ii32;
    BOOL16	ret = GetIconInfo32((HICON32)hIcon, &ii32);

    iconinfo->fIcon = ii32.fIcon;
    iconinfo->xHotspot = ii32.xHotspot;
    iconinfo->yHotspot = ii32.yHotspot;
    iconinfo->hbmMask = ii32.hbmMask;
    iconinfo->hbmColor = ii32.hbmColor;
    return ret;
}

/**********************************************************************
 *          GetIconInfo32		(USER32.242)
 */
BOOL32 WINAPI GetIconInfo32(HICON32 hIcon,LPICONINFO32 iconinfo) {
    CURSORICONINFO	*ciconinfo;

    ciconinfo = GlobalLock16(hIcon);
    if (!ciconinfo)
	return FALSE;
    iconinfo->xHotspot = ciconinfo->ptHotSpot.x;
    iconinfo->yHotspot = ciconinfo->ptHotSpot.y;
    iconinfo->fIcon    = TRUE; /* hmm */

    iconinfo->hbmColor = CreateBitmap32 ( ciconinfo->nWidth, ciconinfo->nHeight,
                                ciconinfo->bPlanes, ciconinfo->bBitsPerPixel,
                                (char *)(ciconinfo + 1)
                                + ciconinfo->nHeight *
                                BITMAP_GetWidthBytes (ciconinfo->nWidth,1) );
    iconinfo->hbmMask = CreateBitmap32 ( ciconinfo->nWidth, ciconinfo->nHeight,
                                1, 1, (char *)(ciconinfo + 1));

    GlobalUnlock16(hIcon);

    return TRUE;
}

/**********************************************************************
 *          CreateIconIndirect		(USER32.78)
 */
HICON32 WINAPI CreateIconIndirect(LPICONINFO32 iconinfo) {
    BITMAPOBJ *bmpXor,*bmpAnd;
    HICON32 hObj;
    int	sizeXor,sizeAnd;

    bmpXor = (BITMAPOBJ *) GDI_GetObjPtr( iconinfo->hbmColor, BITMAP_MAGIC );
    bmpAnd = (BITMAPOBJ *) GDI_GetObjPtr( iconinfo->hbmMask, BITMAP_MAGIC );

    sizeXor = bmpXor->bitmap.bmHeight * bmpXor->bitmap.bmWidthBytes;
    sizeAnd = bmpAnd->bitmap.bmHeight * bmpAnd->bitmap.bmWidthBytes;

    hObj = GlobalAlloc16( GMEM_MOVEABLE, 
		     sizeof(CURSORICONINFO) + sizeXor + sizeAnd );
    if (hObj)
    {
	CURSORICONINFO *info;

	info = (CURSORICONINFO *)GlobalLock16( hObj );
	info->ptHotSpot.x   = iconinfo->xHotspot;
	info->ptHotSpot.y   = iconinfo->yHotspot;
	info->nWidth        = bmpXor->bitmap.bmWidth;
	info->nHeight       = bmpXor->bitmap.bmHeight;
	info->nWidthBytes   = bmpXor->bitmap.bmWidthBytes;
	info->bPlanes       = bmpXor->bitmap.bmPlanes;
	info->bBitsPerPixel = bmpXor->bitmap.bmBitsPixel;

	/* Transfer the bitmap bits to the CURSORICONINFO structure */

	GetBitmapBits32( iconinfo->hbmMask ,sizeAnd,(char*)(info + 1) );
	GetBitmapBits32( iconinfo->hbmColor,sizeXor,(char*)(info + 1) +sizeAnd);
	GlobalUnlock16( hObj );
    }
    return hObj;
}


/**********************************************************************
 *          
 DrawIconEx16		(USER.394)
 */
BOOL16 WINAPI DrawIconEx16 (HDC16 hdc, INT16 xLeft, INT16 yTop, HICON16 hIcon,
			    INT16 cxWidth, INT16 cyWidth, UINT16 istep,
			    HBRUSH16 hbr, UINT16 flags)
{
    return DrawIconEx32(hdc, xLeft, yTop, hIcon, cxWidth, cyWidth,
                        istep, hbr, flags);
}


/******************************************************************************
 * DrawIconEx32 [USER32.160]  Draws an icon or cursor on device context
 *
 * NOTES
 *    Why is this using SM_CXICON instead of SM_CXCURSOR?
 *
 * PARAMS
 *    hdc     [I] Handle to device context
 *    x0      [I] X coordinate of upper left corner
 *    y0      [I] Y coordinate of upper left corner
 *    hIcon   [I] Handle to icon to draw
 *    cxWidth [I] Width of icon
 *    cyWidth [I] Height of icon
 *    istep   [I] Index of frame in animated cursor
 *    hbr     [I] Handle to background brush
 *    flags   [I] Icon-drawing flags
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 */
BOOL32 WINAPI DrawIconEx32( HDC32 hdc, INT32 x0, INT32 y0, HICON32 hIcon,
                            INT32 cxWidth, INT32 cyWidth, UINT32 istep, 
                            HBRUSH32 hbr, UINT32 flags )
{
    CURSORICONINFO *ptr = (CURSORICONINFO *)GlobalLock16 (hIcon);
    HDC32 hDC_off = 0, hMemDC = CreateCompatibleDC32 (hdc);
    BOOL32 result = FALSE, DoOffscreen = FALSE;
    HBITMAP32 hB_off = 0, hOld = 0;

    if (!ptr) return FALSE;

    if (istep)
        FIXME(icon, "Ignoring istep=%d\n", istep);
    if (flags & DI_COMPAT)
        FIXME(icon, "Ignoring flag DI_COMPAT\n");

    /* Calculate the size of the destination image.  */
    if (cxWidth == 0)
    {
      if (flags & DI_DEFAULTSIZE)
	cxWidth = GetSystemMetrics32 (SM_CXICON);
      else
	cxWidth = ptr->nWidth;
    }
    if (cyWidth == 0)
    {
      if (flags & DI_DEFAULTSIZE)
        cyWidth = GetSystemMetrics32 (SM_CYICON);
      else
	cyWidth = ptr->nHeight;
    }

    if (!(DoOffscreen = (hbr >= STOCK_WHITE_BRUSH) && (hbr <= 
      STOCK_HOLLOW_BRUSH)))
    {
	GDIOBJHDR *object = (GDIOBJHDR *) GDI_HEAP_LOCK(hbr);
	if (object)
	{
	    UINT16 magic = object->wMagic;
	    GDI_HEAP_UNLOCK(hbr);
	    DoOffscreen = magic == BRUSH_MAGIC;
	}
    }
    if (DoOffscreen) {
      RECT32 r = {0, 0, cxWidth, cxWidth};

      hDC_off = CreateCompatibleDC32(hdc);
      hB_off = CreateCompatibleBitmap32(hdc, cxWidth, cyWidth);
      if (hDC_off && hB_off) {
	hOld = SelectObject32(hDC_off, hB_off);
	FillRect32(hDC_off, &r, hbr);
      }
    };

    if (hMemDC && (!DoOffscreen || (hDC_off && hB_off)))
    {
	HBITMAP32 hXorBits, hAndBits;
	COLORREF  oldFg, oldBg;
	INT32     nStretchMode;

	nStretchMode = SetStretchBltMode32 (hdc, STRETCH_DELETESCANS);

	hXorBits = CreateBitmap32 ( ptr->nWidth, ptr->nHeight,
				    ptr->bPlanes, ptr->bBitsPerPixel,
				    (char *)(ptr + 1)
				    + ptr->nHeight *
				    BITMAP_GetWidthBytes(ptr->nWidth,1) );
	hAndBits = CreateBitmap32 ( ptr->nWidth, ptr->nHeight,
				    1, 1, (char *)(ptr+1) );
	oldFg = SetTextColor32( hdc, RGB(0,0,0) );
	oldBg = SetBkColor32( hdc, RGB(255,255,255) );

	if (hXorBits && hAndBits)
	{
	    HBITMAP32 hBitTemp = SelectObject32( hMemDC, hAndBits );
	    if (flags & DI_MASK)
            {
	      if (DoOffscreen) 
		StretchBlt32 (hDC_off, 0, 0, cxWidth, cyWidth,
			      hMemDC, 0, 0, ptr->nWidth, ptr->nHeight, SRCAND);
	      else 
	        StretchBlt32 (hdc, x0, y0, cxWidth, cyWidth,
			      hMemDC, 0, 0, ptr->nWidth, ptr->nHeight, SRCAND);
            }
	    SelectObject32( hMemDC, hXorBits );
	    if (flags & DI_IMAGE)
            {
	      if (DoOffscreen) 
		StretchBlt32 (hDC_off, 0, 0, cxWidth, cyWidth,
			  hMemDC, 0, 0, ptr->nWidth, ptr->nHeight, SRCPAINT);
	      else
		StretchBlt32 (hdc, x0, y0, cxWidth, cyWidth,
			      hMemDC, 0, 0, ptr->nWidth, ptr->nHeight, SRCPAINT);
            }
	    SelectObject32( hMemDC, hBitTemp );
	    result = TRUE;
	}

	SetTextColor32( hdc, oldFg );
	SetBkColor32( hdc, oldBg );
	if (hXorBits) DeleteObject32( hXorBits );
	if (hAndBits) DeleteObject32( hAndBits );
	SetStretchBltMode32 (hdc, nStretchMode);
	if (DoOffscreen) {
	  BitBlt32(hdc, x0, y0, cxWidth, cyWidth, hDC_off, 0, 0, SRCCOPY);
	  SelectObject32(hDC_off, hOld);
	}
    }
    if (hMemDC) DeleteDC32( hMemDC );
    if (hDC_off) DeleteDC32(hDC_off);
    if (hB_off) DeleteObject32(hB_off);
    GlobalUnlock16( hIcon );
    return result;
}
