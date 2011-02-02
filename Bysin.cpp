/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "stdafx.h"

/* DumpNAVI by bysin, guicide, and ryebrye
 *
 * Structs and code sniplets taken from Willem Jan Hengeveld <itsme@xs4all.nl>.
 * 
 * This program is used to modify Acura/Honda navigation systems
 * by dumping and modifying system files contained on the DVD.
 *
 *
 * ChangeLog:
 *
 * Feb 2, 2011 -  ryebrye - Ported back to windows (ugh) to handle compressed modules 
 *		          ryebrye - Added 'updateModule' command  with the ability to update 
 *						    compressed modules that are patched. 
 *						
 *
 * Feb 20, 2010 - craig - Ported to Linux, removed dependencies of .dll
 *                        by removing support for compress :(
 *                craig - Added the ability to update modules, allowing updates
 *                        to EXE and DLLs in the archive
 *            
 *            changes by RyeBrye
 *
 * April 3rd, 20?? - guicide - Added optional compress to update large
 *     uncompressed files that normally wouldnt fit.
 */

typedef unsigned char	U8,  *PU8;
typedef unsigned short	U16, *PU16;
typedef unsigned long	U32, *PU32;

typedef signed char		S8,  *PS8;
typedef signed short	S16, *PS16;
typedef signed long		S32, *PS32;

#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <malloc.h>
#include <direct.h>
#include <winnt.h>
#include <Imagehlp.h>

#define VERSION "1.3g2"

#if 0
extern DWORD CEDecompress(LPBYTE BufIn, DWORD InSize, LPBYTE BufOut, DWORD OutSize, DWORD skip, DWORD n, DWORD blocksize);
extern DWORD CECompress(LPBYTE BufIn, DWORD InSize, LPBYTE BufOut, DWORD OutSize, DWORD n, DWORD blocksize);
#else
typedef DWORD (*CEDECOMPRESS)(const LPBYTE  lpbSrc, DWORD cbSrc, LPBYTE  lpbDest, DWORD cbDest, DWORD dwSkip, WORD wStep, DWORD dwPagesize);
typedef DWORD (*CECOMPRESS)(const LPBYTE  lpbSrc, DWORD cbSrc, LPBYTE lpbDest, DWORD cbDest, WORD wStep, DWORD dwPagesize);

CEDECOMPRESS	pDecompress;
CECOMPRESS		pCompress;

#endif

//////////////////////////////////////////////////

struct _romhdr 
	{
	U32 dllfirst;
	U32 dlllast;
	U32 physfirst;
	U32 physlast;
	U32 nummods;
	U32 ulRAMStart;
	U32 ulRAMFree;
	U32 ulRAMEnd;
	U32 ulCopyEntries;
	U32 ulCopyOffset;
	U32 ulProfileLen;
	U32 ulProfileOffset;
	U32 numfiles;
	U32 ulKernelFlags;
	U32 ulFSRamPercent;
	U32 ulDrivglobStart;
	U32 ulDrivglobLen;
	U16 usCPUType;
	U16 usMiscFlags;
	U32 pExtensions;
	U32 ulTrackingStart;
	U32 ulTrackingLen;
	};

struct _blockhdr 
	{
	U32 addr;
	U32 len;
	U32 chksum;
	};

struct _modulehdr 
	{
	U32 attr;
	U32 time;
	U32 time2;
	U32 size;
	char *fileaddr;
	U32 e32offset;
	U32 o32offset;
	U32 offset;
	};

struct _filehdr 
	{
	U32 attr;
	U32 time;
	U32 time2;
	U32 size;
	U32 size2;
	char *fileaddr;
	U32 offset;
	};

enum { FILEATTR_COMPRESS_MODULE=4096, FILEATTR_COMPRESS=2048, FILEATTR_HIDDEN=4, FILEATTR_READONLY=2, FILEATTR_SYSTEM=1 };

enum { COMMAND_LIST=1, COMMAND_EXTRACT=2, COMMAND_UPDATE=3, COMMAND_UPDATE_MODULE=4 };

//////////////////////////////////////////////////

char binfile[256];

U32 imageaddr, imagelen;
fpos_t blockstart;

FILE *f;

U32 romhdraddr;
struct _romhdr romhdr;

struct _modulehdr modules[500];
struct _filehdr files[4000];

//////////////////////////////////////////////////
/* (C) 2003 XDA Developers
 * Author: Willem Jan Hengeveld <itsme@xs4all.nl>
 * Web: http://www.xda-developers.com/
 */

struct e32_info 
	{
	U32 rva;
	U32 size;
	};

#define ROM_EXTRA 9

struct e32_rom 
	{
	U16 e32_objcnt;
	U16 e32_imageflags;
	U32 e32_entryrva;
	U32 e32_vbase;
	U16 e32_subsysmajor;
	U16 e32_subsysminor;
	U32 e32_stackmax;
	U32 e32_vsize;
	U32 e32_sect14rva;
	U32 e32_sect14size;
	struct e32_info e32_unit[ROM_EXTRA];
	U16 e32_subsys;
	};

struct o32_rom 
	{
	U32 o32_vsize;
	U32 o32_rva;
	U32 o32_psize;
	U32 o32_dataptr;
	U32 o32_realaddr;
	U32 o32_flags;
	};

#ifndef IMAGE_DOS_SIGNATURE
#define IMAGE_DOS_SIGNATURE 0x5A4D
typedef struct _IMAGE_DOS_HEADER 
	{
	WORD e_magic;
	WORD e_cblp;
	WORD e_cp;
	WORD e_crlc;
	WORD e_cparhdr;
	WORD e_minalloc;
	WORD e_maxalloc;
	WORD e_ss;
	WORD e_sp;
	WORD e_csum;
	WORD e_ip;
	WORD e_cs;
	WORD e_lfarlc;
	WORD e_ovno;
	WORD e_res[4];
	WORD e_oemid;
	WORD e_oeminfo;
	WORD e_res2[10];
	LONG e_lfanew;
	} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

#define IMAGE_FILE_RELOCS_STRIPPED 0x0001
#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#endif

#define STD_EXTRA       16
#define EXP             0
#define IMP             1
#define RES             2
#define EXC             3
#define SEC             4
#define FIX             5
#define DEB             6
#define IMD             7
#define MSP             8
#define TLS             9
#define CBK            10
#define RS1            11
#define RS2            12
#define RS3            13
#define RS4            14
#define RS5            15

struct e32_exe 
	{
	U8	e32_magic[4];
	U16	e32_cpu;
	U16	e32_objcnt;
	U32	e32_timestamp;
	U32	e32_symtaboff;
	U32	e32_symcount;
	U16	e32_opthdrsize;
	U16	e32_imageflags;
	U16	e32_coffmagic;
	U8	e32_linkmajor;
	U8	e32_linkminor;
	U32	e32_codesize;
	U32	e32_initdsize;
	U32	e32_uninitdsize;
	U32	e32_entryrva;
	U32	e32_codebase;
	U32	e32_database;
	U32	e32_vbase;
	U32	e32_objalign;
	U32	e32_filealign;
	U16	e32_osmajor;
	U16	e32_osminor;
	U16	e32_usermajor;
	U16	e32_userminor;
	U16	e32_subsysmajor;
	U16	e32_subsysminor;
	U32	e32_res1;
	U32	e32_vsize;
	U32	e32_hdrsize;
	U32	e32_filechksum;
	U16	e32_subsys;
	U16	e32_dllflags;
	U32	e32_stackmax;
	U32	e32_stackinit;
	U32	e32_heapmax;
	U32	e32_heapinit;
	U32	e32_res2;
	U32	e32_hdrextra;
	struct e32_info e32_unit[STD_EXTRA];
	};
#define E32OBJNAMEBYTES 8

struct o32_obj 
	{
	U8 o32_name[E32OBJNAMEBYTES];
	U32 o32_vsize;
	U32 o32_rva;
	U32 o32_psize;
	U32 o32_dataptr;
	U32 o32_realaddr;
	U32 o32_access;
	U32 o32_temp3;
	U32 o32_flags;
	};

#define ST_TEXT  0
#define ST_DATA  1
#define ST_PDATA 2
#define ST_RSRC  3
#define ST_OTHER 4
DWORD g_segmentNameUsage[5];
char *g_segmentNames[5]= { ".text", ".data", ".pdata", ".rsrc", ".other" };

U8 doscode[]= {
	0x0e, 0x1f, 0xba, 0x0e, 0x00, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0x01, 0x4c, 0xcd, 0x21, 0x54, 0x68,
	0x69, 0x73, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e, 0x6f,
	0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6e, 0x20, 0x69, 0x6e, 0x20, 0x44, 0x4f, 0x53, 0x20,
	0x6d, 0x6f, 0x64, 0x65, 0x2e, 0x0d, 0x0d, 0x0a, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

//////////////////////////////////////////////////

U32 virtualpos=0,blocklen=0;
fpos_t blockstartpos=0;

U32 VirtualSeek(U32 addr) 
{
	fsetpos(f,&blockstart);
	while(!feof(f)) {
		struct _blockhdr b;
		fgetpos(f,&blockstartpos);
		fread(&b,sizeof(struct _blockhdr),1,f);
		if (!b.addr) break;
		if (addr >= b.addr && addr < b.addr+b.len) {
			int a=addr-b.addr;
			fseek(f,a,SEEK_CUR);
			blocklen=b.len-a;
			virtualpos=addr;
			return blocklen;
		}
		fseek(f,b.len,SEEK_CUR);
	}
	blocklen=0;
	blockstartpos=0;
	virtualpos=0;
	return blocklen;
}

U32 VirtualRead(char *buf, U32 size) 
{
	int pos=0, origsize=size;
	if (!virtualpos) {
		pos=fread(buf,1,size,f);
		if (pos <= 0) return 0;
		return pos;
	}
	if (blocklen >= size) {
		blocklen-=size;
		virtualpos+=size;
		pos=fread(buf,1,size,f);
		if (pos <= 0) return 0;
		return pos;
	}
	while(size) {
		if (blocklen) {
			int a;
			if (blocklen < size) a=blocklen;
			else a=size;
			if (fread(buf+pos,1,a,f) <= 0) return 0;
			size-=a;
			virtualpos+=a;
			pos+=a;
		}
		if (!VirtualSeek(virtualpos)) break;
	}
	return pos;
}

U32 VirtualCalcSum() 
{
	fpos_t savepos;
	U32 chksum;
	PU8 buf, pos;
	struct _blockhdr b;

	fgetpos(f,&savepos);
	fsetpos(f,&blockstartpos);
	fread(&b,sizeof(struct _blockhdr),1,f);
	buf = (PU8)malloc(b.len);
	fread(buf,b.len,1,f);

	for (pos=buf, chksum=0;(U32)(pos-buf) < (U32)b.len; pos++) 
		chksum += *pos;
	free(buf);
	b.chksum=chksum;
	fsetpos(f,&blockstartpos);
	fwrite(&b,sizeof(struct _blockhdr),1,f);
	fsetpos(f,&savepos);
	return 1;
}

U32 VirtualWrite(char *buf, U32 size) {
	int pos=0,origsize=size;
	if (!virtualpos) {
		pos=fwrite(buf,1,size,f);
		VirtualCalcSum();
		if (pos <= 0) return 0;
		return pos;
	}

	if (blocklen >= size) {
		blocklen-=size;
		virtualpos+=size;
		pos=fwrite(buf,1,size,f);
		VirtualCalcSum();
		if (pos <= 0) return 0;
		return pos;
	}
	while(size) {
		if (blocklen) {
			int a;
			if (blocklen < size) a=blocklen;
			else a=size;
			if (fwrite(buf+pos,1,a,f) <= 0) {
				VirtualCalcSum();
				return 0;
			}
			VirtualCalcSum();
			size-=a;
			virtualpos+=a;
			pos+=a;
		}
		if (!VirtualSeek(virtualpos)) break;
	}
	return pos;
}
int UpdateFileSize(int file,int size,int size2,int attr) {
	struct _filehdr fh;
	fpos_t p;
	if (!VirtualSeek(romhdraddr+sizeof(struct _romhdr)+(sizeof(struct _modulehdr)*romhdr.nummods)+(sizeof(struct _filehdr)*file))) {
		printf("Unable to read block file\n");
		return 0;
	}
	fgetpos(f,&p);
	VirtualRead((char*)&fh,sizeof(struct _filehdr));
	fh.size=size;
	fh.size2=size2;
	fh.attr=attr;
	fsetpos(f,&p);
	VirtualWrite((char*)&fh,sizeof(struct _filehdr));
	return 1;
}

//////////////////////////////////////////////////

int ReadHeader() {
	char buf[7];
	VirtualRead(buf,7);
	if (strncmp(buf,"B000FF\n",7)) return 0;
	VirtualRead((char*)&imageaddr,4);
	VirtualRead((char*)&imagelen,4);
	fgetpos(f,&blockstart);
	return 1;
}
int ReadECEC() {
	char buf[4];
	if (!VirtualSeek(imageaddr+0x40)) return 0;
	VirtualRead(buf,4);
	if (strncmp(buf,"ECEC",4)) return 0;
	VirtualRead((char*)&romhdraddr,4);
	return 1;
}
int ReadRomHdr() {
	if (!VirtualSeek(romhdraddr)) return 0;
	VirtualRead((char*)&romhdr,sizeof(struct _romhdr));
	return 1;
}

int ReadModules(int command,int argc, char **argv) {
	U32 i;
	if (!VirtualSeek(romhdraddr+sizeof(struct _romhdr))) {
		printf("Unable to read block file\n");
		return 0;
	}

	for (i=0; i<romhdr.nummods; i++) 
		VirtualRead((char*)&modules[i],sizeof(struct _modulehdr));
	
	for (i=0;i<romhdr.nummods;i++) {
		char name[1024],*ptr;
		if (!VirtualSeek((U32)modules[i].fileaddr)) {
			printf("Unable to read block file\n");
			return 0;
		}
		for (ptr=name;(ptr-name)<sizeof(name);ptr++) if (!(*ptr=getc(f))) break;
		modules[i].fileaddr=strdup(name);
		if (command == COMMAND_LIST) {
			printf("%c%c%c%c%10d%10s%22s (ROM 0x%08x)\n",
				modules[i].attr&FILEATTR_COMPRESS_MODULE?'C':'_',
				modules[i].attr&FILEATTR_HIDDEN?'H':'_',
				modules[i].attr&FILEATTR_READONLY?'R':'_',
				modules[i].attr&FILEATTR_SYSTEM?'S':'_',
				modules[i].size,"",modules[i].fileaddr,modules[i].offset);
		}
		else if (command == COMMAND_EXTRACT) {
			struct e32_rom e32hdr;
			struct o32_rom o32hdr[32];
			struct e32_exe pe32;
			struct o32_obj po32;
			IMAGE_DOS_HEADER dos;
			FILE *r;
			char *buf;
			U32 newe32off,headersize,filesize,size,j;
			U32 o32hdroff[32];
			__int64 t;

#if 1 // extract everything
			if (argc) 
				{
				int j,k=0;
				for (j=0;j<argc;j++) 
					if (!strcmp(argv[j],modules[i].fileaddr)) 
						{
						k=1;
						break;
						}
				if (!k) 
					continue;
				}
#endif
			
			printf("Extracting %s ...\n",modules[i].fileaddr);
			mkdir(binfile);
			sprintf(name,"%s/%s",binfile,modules[i].fileaddr);
			if (!(r=fopen(name,"wb"))) {
				printf("Unable to open %s\n",name);
				return 0;
			}
			if (!VirtualSeek(modules[i].e32offset)) {
				printf("Unable to locate e32offset\n");
				return 0;
			}
			VirtualRead((char*)&e32hdr,sizeof(struct e32_rom));
			if (!VirtualSeek(modules[i].o32offset)) {
				printf("Unable to locate o32offset\n");
				return 0;
			}
			for (j=0;j<e32hdr.e32_objcnt;j++) VirtualRead((char*)&o32hdr[j],sizeof(struct o32_rom));
			memset(&dos, 0, sizeof(dos));
			dos.e_magic = IMAGE_DOS_SIGNATURE;
			dos.e_cblp = 0x90;
			dos.e_cp = 3;
			dos.e_cparhdr = 0x4;
			dos.e_maxalloc = 0xffff;
			dos.e_sp = 0xb8;
			dos.e_lfarlc = 0x40;
			dos.e_lfanew = 0xc0;
			fwrite(&dos, 1, sizeof(dos), r);
			fwrite(&doscode, 1, sizeof(doscode), r);
			fseek(r, 0x40, SEEK_CUR);
			newe32off=ftell(r);
			memset(&pe32, 0, sizeof(pe32));
			pe32.e32_magic[0]= 'P';
			pe32.e32_magic[1]= 'E';
			// this sets the cpu type - but doesn't seem necessary
			//pe32.e32_cpu= 0x0300;
			pe32.e32_objcnt= e32hdr.e32_objcnt;
			t=modules[i].time;
			t<<=32;
			t|=modules[i].time2;
			t /= (LONGLONG)10000000L;
			t -= (LONGLONG)11644473600L;
			pe32.e32_timestamp = (U32)t;
			pe32.e32_symtaboff=0;
			pe32.e32_symcount=0;
			pe32.e32_opthdrsize= 0xe0;
			pe32.e32_imageflags= e32hdr.e32_imageflags | IMAGE_FILE_RELOCS_STRIPPED;
			pe32.e32_coffmagic= 0x10b;
			pe32.e32_linkmajor= 6;
			pe32.e32_linkminor= 1;
			for (j=0,size=0;j<e32hdr.e32_objcnt;j++) if (o32hdr[i].o32_flags&IMAGE_SCN_CNT_CODE) size+=o32hdr[i].o32_vsize;
			pe32.e32_codesize=size;
			for (j=0,size=0;j<e32hdr.e32_objcnt;j++) if (o32hdr[i].o32_flags&IMAGE_SCN_CNT_INITIALIZED_DATA) size+=o32hdr[i].o32_vsize;
			pe32.e32_initdsize=size;
			for (j=0,size=0;j<e32hdr.e32_objcnt;j++) if (o32hdr[i].o32_flags&IMAGE_SCN_CNT_UNINITIALIZED_DATA) size+=o32hdr[i].o32_vsize;
			pe32.e32_uninitdsize=size;
			pe32.e32_entryrva= e32hdr.e32_entryrva;
			for (j=0,size=0;j<e32hdr.e32_objcnt;j++) if (o32hdr[i].o32_flags&IMAGE_SCN_CNT_CODE) {
				size=o32hdr[i].o32_vsize;
				break;
			}
			pe32.e32_codebase=size;
			for (j=0,size=0;j<e32hdr.e32_objcnt;j++) if (o32hdr[i].o32_flags&IMAGE_SCN_CNT_INITIALIZED_DATA) {
				size=o32hdr[i].o32_vsize;
				break;
			}
			pe32.e32_database=size;
			pe32.e32_vbase= e32hdr.e32_vbase;
			pe32.e32_objalign= 0x1000;
			pe32.e32_filealign= 0x200;
			pe32.e32_osmajor= 4;
			pe32.e32_osminor= 0;
			pe32.e32_subsysmajor= e32hdr.e32_subsysmajor;
			pe32.e32_subsysminor= e32hdr.e32_subsysminor;
			pe32.e32_vsize= e32hdr.e32_vsize;
			pe32.e32_filechksum= 0;
			pe32.e32_subsys= e32hdr.e32_subsys;
			pe32.e32_stackmax= e32hdr.e32_stackmax;
			pe32.e32_stackinit=0x1000;
			pe32.e32_heapmax=0x100000;
			pe32.e32_heapinit=0x1000;
			pe32.e32_hdrextra=STD_EXTRA;
			pe32.e32_unit[EXP]= e32hdr.e32_unit[EXP];
			pe32.e32_unit[IMP]= e32hdr.e32_unit[IMP];
			pe32.e32_unit[RES]= e32hdr.e32_unit[RES];
			pe32.e32_unit[EXC]= e32hdr.e32_unit[EXC];
			pe32.e32_unit[SEC]= e32hdr.e32_unit[SEC];
			pe32.e32_unit[IMD]= e32hdr.e32_unit[IMD];
			pe32.e32_unit[MSP]= e32hdr.e32_unit[MSP];
			pe32.e32_unit[RS4].rva= e32hdr.e32_sect14rva;
			pe32.e32_unit[RS4].size= e32hdr.e32_sect14size;
			fwrite(&pe32, 1, sizeof(pe32), r);
			for (j=0;j<e32hdr.e32_objcnt;j++) {
				int segtype;
				o32hdroff[j]=ftell(r);
				memset(&po32, 0, sizeof(po32));
				if (e32hdr.e32_unit[RES].rva==o32hdr[j].o32_rva && e32hdr.e32_unit[RES].size==o32hdr[j].o32_vsize) segtype= ST_RSRC;
				else if (e32hdr.e32_unit[EXC].rva==o32hdr[j].o32_rva && e32hdr.e32_unit[EXC].size==o32hdr[j].o32_vsize) segtype= ST_PDATA;
				else if (o32hdr[j].o32_flags&IMAGE_SCN_CNT_CODE) segtype= ST_TEXT;
				else if (o32hdr[j].o32_flags&IMAGE_SCN_CNT_INITIALIZED_DATA) segtype= ST_DATA;
				else if (o32hdr[j].o32_flags&IMAGE_SCN_CNT_UNINITIALIZED_DATA) segtype= ST_PDATA;
				else segtype= ST_OTHER;
				if (g_segmentNameUsage[segtype]) _snprintf((char*)po32.o32_name, 8, "%s%ld", g_segmentNames[segtype], g_segmentNameUsage[segtype]);
				else _snprintf((char*)po32.o32_name, 8, "%s", g_segmentNames[segtype]);
				g_segmentNameUsage[segtype]++;
				po32.o32_vsize= o32hdr[j].o32_vsize;
				po32.o32_rva= o32hdr[j].o32_rva;
				po32.o32_psize= 0;
				po32.o32_dataptr= 0;
				po32.o32_realaddr= 0;
				po32.o32_access= 0;
				po32.o32_temp3= 0;
				po32.o32_flags= o32hdr[j].o32_flags & ~0x2000;
				fwrite(&po32, 1, sizeof(po32), r);
			}
			size=ftell(r);
			if (size%0x200) fseek(r, 0x200-(size%0x200), SEEK_CUR);
			headersize=ftell(r);
			for (j=0;j<e32hdr.e32_objcnt;j++) {
				U32 dataofslist,datalenlist=o32hdr[j].o32_psize;
				dataofslist=ftell(r);
				buf=(char*)malloc(o32hdr[j].o32_psize);
				if (!VirtualSeek(o32hdr[j].o32_dataptr)) {
					printf("Unable to read block file\n");
					return 0;
				}
				VirtualRead(buf,o32hdr[j].o32_psize);
				if (o32hdr[j].o32_flags & 0x2000) {
					char *out;
					long outlen;
					out=(char*)malloc(o32hdr[j].o32_vsize);
					outlen = (*pDecompress)((BYTE*)buf, o32hdr[j].o32_psize, (BYTE*)out, o32hdr[j].o32_vsize, 0, 1, 4096);
					if (outlen < 0) printf("Error in CEDecompress()\n");
					else fwrite(out,1,outlen,r);
					free(out);
					datalenlist=outlen;
				}
				else fwrite(buf,1,o32hdr[j].o32_psize,r);
				free(buf);
				size=ftell(r);
				if (size%0x200) fseek(r, 0x200-(size%0x200), SEEK_CUR);
		        fseek(r, o32hdroff[j]+16, SEEK_SET);
		        fwrite(&datalenlist, 1, sizeof(U32), r);
		        fwrite(&dataofslist, 1, sizeof(U32), r);
				fseek(r,0,SEEK_END);
			}
			filesize=ftell(r);
			fseek(r, newe32off+0x54, SEEK_SET);
			fwrite(&headersize, 1, sizeof(U32), r);
			fseek(r, filesize, SEEK_SET);
			fclose(r);
		}
		else if (command == COMMAND_UPDATE_MODULE) {
			FILE *updateModuleInputFile;
			struct _stat moduleStat;


			// if the current module matches the name continue
			if (strcmp(argv[0],modules[i].fileaddr)) continue;

			printf("Updating module %s...\n", modules[i].fileaddr);

			char *fname = ((argc >= 2) ? argv[1] : argv[0]);

			// load file and parse its header
			updateModuleInputFile = fopen(fname, "rb");

			struct e32_rom e32hdr;
			struct o32_rom o32hdr[32];

			if (!VirtualSeek(modules[i].e32offset)) {
				printf("Unable to locate e32offset\n");
				return 0;
			}

			VirtualRead((char*)&e32hdr,sizeof(struct e32_rom));
			if (!VirtualSeek(modules[i].o32offset)) {
				printf("Unable to locate o32offset\n");
				return 0;
			}
			for (unsigned long j=0;j<e32hdr.e32_objcnt;j++) {
				VirtualRead((char*)&o32hdr[j],sizeof(struct o32_rom));
			}


			// parse section info with the help of imagehlp.dll to determine 
			// if the file has the same number of sections as the split up module in the 
			// module section (if they don't, then you can't replace the one with this one
			// - at least not currently - since doing so would involve a lot more work)
			LOADED_IMAGE updatedModuleLoadedImage;
			if (MapAndLoad(fname, 0, &updatedModuleLoadedImage, true,true)){
				if (updatedModuleLoadedImage.NumberOfSections != e32hdr.e32_objcnt) {
					printf("Error - can't update the module");
					printf("Incoming module %s has %d sections, existing module in rom has %d sections.\n",
							fname,
							updatedModuleLoadedImage.NumberOfSections,
							e32hdr.e32_objcnt);
					return 0;
				}
				U32 headersize = sizeof(IMAGE_DOS_HEADER) + sizeof(doscode) + sizeof(struct e32_exe) + (sizeof(struct o32_obj) * e32hdr.e32_objcnt);
				if(headersize%0x200) headersize+=0x200-(headersize%0x200);

				// seek past the header
				fseek(updateModuleInputFile, headersize, SEEK_SET);
				char *sectionData;
				U32 size;
				for (int s=0;s<updatedModuleLoadedImage.NumberOfSections;s++) {
					
					IMAGE_SECTION_HEADER currentSectionHeader = updatedModuleLoadedImage.Sections[s];
					unsigned long sectionDataSize = currentSectionHeader.SizeOfRawData;

					sectionData = (char*)malloc(sectionDataSize+0x400);

#ifdef DEBUG_DUMP_DATA 					
					printf("Current section name: %s\t size: 0x%08x physical address: 0x%08x\n",
							currentSectionHeader.Name,
							currentSectionHeader.SizeOfRawData,
							currentSectionHeader.Misc.PhysicalAddress);
					printf("stored module section number: %1d\t psize: 0x%08x vsize: 0x%08x data ptr: 0x%08x, real addr: 0x%08x\n",
							s,
							o32hdr[s].o32_psize,
							o32hdr[s].o32_vsize,
							o32hdr[s].o32_dataptr,
							o32hdr[s].o32_realaddr);

					char* buf=(char*)malloc(o32hdr[s].o32_psize);
					if (!VirtualSeek(o32hdr[s].o32_dataptr)) {
						printf("Unable to read block file\n");
						return 0;
					}
					VirtualRead(buf,o32hdr[s].o32_psize);
#endif
					fread(sectionData,1,sectionDataSize,updateModuleInputFile);
					if (ferror(updateModuleInputFile)){
						printf("error");
					}

					// if this section is compressed
					if (o32hdr[s].o32_flags & 0x2000) {
						char *compressedSection;
						compressedSection = (char*)malloc(sectionDataSize+20);
#ifdef DEBUG_DUMP_DATA 	
						printf("section %d is compressed\n",s);
#endif
						long compressedLen=(*pCompress)((BYTE*)sectionData,
														sectionDataSize,
														(BYTE*)compressedSection,
														sectionDataSize+20,
														1,
														4096);
						if (compressedLen > o32hdr[s].o32_psize) {
							printf("Compressing section %s of module %s results in a section too big to fit.\n", currentSectionHeader.Name, fname);
							printf("(Compressed size: %10d - existing section size %10d)", compressedLen, o32hdr[s].o32_psize);
							return 0;
						}
						if (compressedLen != o32hdr[s].o32_psize) {
							printf("not an exact match in compressed size of updated module and the existing module.\n");
							printf("the crap might hit the fan.");
						}
						//free(sectionData);
						sectionData = compressedSection;
						sectionDataSize = compressedLen;

					}
					// move the .bin file pointer to the correct spot for writing this section
					if (!VirtualSeek(o32hdr[s].o32_dataptr)) {
						printf("Unable to read block file (the .bin file)\n");
						return 0;
					}
					VirtualWrite(sectionData, sectionDataSize);
					free(sectionData);

#ifdef DEBUG_DUMP_DATA
					FILE* moduleFileDump;
					FILE* readFileDump;
					mkdir(binfile);
					char dumpname[1024];
					char readname[1024];
					sprintf(dumpname,"%s/%sB%d",binfile,modules[i].fileaddr,s);
					sprintf(readname,"%s/%sU%d",binfile,modules[i].fileaddr,s);

					if (!(moduleFileDump=fopen(dumpname,"wb"))) {
						printf("Unable to open %s\n",name);
						return 0;
					}
					if (!(readFileDump=fopen(readname,"wb"))) {
						printf("Unable to open %s\n",name);
						return 0;
					}
					fwrite(buf,o32hdr[s].o32_psize,1,moduleFileDump);
					fwrite(sectionData,sectionDataSize,1,readFileDump);
					fclose(moduleFileDump);
					fclose(readFileDump); */
#endif
					
					size=ftell(updateModuleInputFile);
					// this code was in cerom - but doesn't seem to be necessary
					//if (size%0x200) fseek(updateModuleInputFile, 0x200-(size%0x200), SEEK_CUR);
				}
			}
			else {
				printf("Error - can't map and load update module %s", fname);
				printf("MapAndUnload problem = %d",GetLastError());
				return 0;
			}
			printf("Successfully updated module.", fname);
		}
	}
	return 1;
}

int ReadFiles(int command,int argc, char **argv) {
	U32 i,didupdate=0;
	if (!VirtualSeek(romhdraddr+sizeof(struct _romhdr)+(sizeof(struct _modulehdr)*romhdr.nummods))) {
		printf("Unable to read block file\n");
		return 0;
	}

	for (i=0;i<romhdr.numfiles;i++) 
		VirtualRead((char*)&files[i],sizeof(struct _filehdr));

	for (i=0;i<romhdr.numfiles;i++) {
		char name[1024],*ptr;
		if (!VirtualSeek((U32)files[i].fileaddr)) {
			printf("Unable to read block file\n");
			return 0;
		}
		for (ptr=name;(ptr-name)<sizeof(name);ptr++) if (!(*ptr=getc(f))) break;
		files[i].fileaddr=strdup(name);

		if (command == COMMAND_LIST) {
			printf("%c%c%c%c%10d%10d%22s (ROM 0x%08x)\n",
				files[i].attr&FILEATTR_COMPRESS?'C':'_',
				files[i].attr&FILEATTR_HIDDEN?'H':'_',
				files[i].attr&FILEATTR_READONLY?'R':'_',
				files[i].attr&FILEATTR_SYSTEM?'S':'_',
				files[i].size,files[i].size2,files[i].fileaddr,files[i].offset);
		}
		else if (command == COMMAND_EXTRACT) {
			FILE *r;
			char *buf;
			if (argc) {
				int j,k=0;
				for (j=0;j<argc;j++) if (!strcmp(argv[j],files[i].fileaddr)) {
					k=1;
					break;
				}
				if (!k) continue;
			}
			printf("Extracting %s ...\n",files[i].fileaddr);
			mkdir(binfile);
			sprintf(name,"%s/%s",binfile,files[i].fileaddr);
			if (!VirtualSeek(files[i].offset)) {
				printf("Unable to read block file\n");
				return 0;
			}
			if (!(r=fopen(name,"wb"))) {
				printf("Unable to open %s\n",name);
				return 0;
			}
			buf=(char*)malloc(files[i].size2);
			VirtualRead(buf,files[i].size2);
			if (files[i].attr & FILEATTR_COMPRESS) {
				char *out;
				long outlen;
				out=(char*)malloc(files[i].size);
				outlen = (*pDecompress)((BYTE*)buf, files[i].size2, (BYTE*)out, files[i].size, 0, 1, 4096);
				if (outlen < 0) printf("Error in CEDecompress()\n");
				else fwrite(out,1,outlen,r);
				free(out);
			}
			else fwrite(buf,1,files[i].size2,r);
			free(buf);
			fclose(r);
		}
		else if (command == COMMAND_UPDATE) {
			FILE *r;
			char *buf,*fname;
			struct _stat s;
			if (strcmp(argv[0],files[i].fileaddr)) continue;
			didupdate=1;
			if (argc >= 2) fname=argv[1];
			else fname=argv[0];
			printf("Updating %s ...\n",files[i].fileaddr);
			if (!VirtualSeek(files[i].offset)) {
				printf("Unable to read block file\n");
				return 0;
			}
			if (_stat(fname, &s) < 0) {
				printf("Unable to stat file %s\n",fname);
				return 0;
			}
			if (!(r=fopen(fname,"rb"))) {
				printf("Unable to open file %s\n",fname);
				return 0;
			}
			buf=(char*)malloc(s.st_size);
			fread(buf,1,s.st_size,r);
			if (!(files[i].attr & FILEATTR_COMPRESS)) {
				if ((U32)s.st_size > files[i].size2) {
					files[i].attr|=FILEATTR_COMPRESS;
				}
				else {
					VirtualWrite(buf,s.st_size);
					if (!UpdateFileSize(i,s.st_size,s.st_size,files[i].attr)) return 0;
				}
			}
			if (files[i].attr & FILEATTR_COMPRESS) {
				char *out;
				long outlen;
				out=(char*)malloc(s.st_size+20);
				outlen = (*pCompress)((BYTE*)buf, s.st_size, (BYTE*)out, s.st_size+20, 1, 4096);
				if (outlen < 0) printf("Error in CECompress()\n");
				else {
					if ((U32)outlen > files[i].size2) {
						printf("The size of the updated file must be less-then or equal to the size of the old file.\n");
						printf("This feature might be introduced in a newer version.\n");
						printf("Updated file (COMPRESSED): %d bytes   Old file (COMPRESSED): %d bytes\n",outlen,files[i].size2);
					}
					else {
						VirtualWrite(out,outlen);
						if (!UpdateFileSize(i,s.st_size,outlen,files[i].attr)) return 0;
					}
				}
				free(out);
			}
			free(buf);
			fclose(r);
		}
	}
	if (!didupdate && command == COMMAND_UPDATE) {
		printf("Unable to find %s on BIN\n",argv[0]);
		return 0;
	}
	return 1;
}

void usage(char *a) 
	{
	printf("Bysin "VERSION" by bysin (ported by guicide, updated by ryebrye)\n\n");
	printf("%s <filename> <command>\n",a);
	printf("Valid commands are:\n");
	printf("  list                    - lists contents\n");
	printf("  extract [files...]      - extract all/specified files\n");
	printf("  update outfile [infile] - update specified files\n");
	printf("  updateModule outfile [infile] - update specified module\n");
	}

int main(int argc, char **argv) 
{
	int command;
	char *ptr,*ptr2;

	if (argc <= 2) 
		{
		usage(argv[0]);
		return 0;
		}

	if (!stricmp(argv[2],"list")) 
		command=COMMAND_LIST;
	else if (!stricmp(argv[2],"extract")) 
		command=COMMAND_EXTRACT;
	else if (!stricmp(argv[2],"update") && argc >= 4) 
		command=COMMAND_UPDATE;
	else if (!stricmp(argv[2],"updateModule") && argc >= 4) 
		command=COMMAND_UPDATE_MODULE;
	else 
		{
		usage(argv[0]);
		return 0;
		}

	for (ptr=argv[1],ptr2=binfile;*ptr && *ptr != '.';ptr++,ptr2++) 
		*ptr2 = *ptr;

	*ptr2=0;
	
	if (!strcmp(binfile,argv[1])) 
		{
		*ptr2++='-';
		*ptr2=0;
		}
	if (!(f=fopen(argv[1],"r+b"))) 
		{
		printf("Unable to open BIN file\n");
		return 0;
		}


	if ((command == COMMAND_EXTRACT) || (command == COMMAND_UPDATE) || (command == COMMAND_UPDATE_MODULE))
		{
		HMODULE	hModule;

#if 0
		hModule = LoadLibrary("CECompress.dll");
#else
		hModule = LoadLibrary("CECompressv4.dll");
#endif

		if ((hModule == NULL) || (hModule == INVALID_HANDLE_VALUE)) 
			{
			printf("?Can't load Compression library\n");
			return 0;
			}

		pCompress   = (CECOMPRESS)GetProcAddress(hModule, "CECompress");
		pDecompress = (CEDECOMPRESS)GetProcAddress(hModule, "CEDecompress");
		}


	if (!ReadHeader()) 
		{
		printf("Invalid XIP file\n");
		return 0;
		}
	if (!ReadECEC()) 
		{
		printf("Invalid ECEC header\n");
		return 0;
		}
	if (!ReadRomHdr()) 
		{
		printf("Invalid ROM header\n");
		return 0;
		}

	ReadModules(command,argc-3,argv+3);
	
	ReadFiles(command,argc-3,argv+3);
	_fcloseall();

	Sleep(1000);
	return 0;
}
