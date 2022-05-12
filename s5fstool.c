/* s5fstool - Dump TI S1500 SVR3 file systems
 *
 * Written by Michael Engel <engel@multicores.org>
 * Modified by Jeffrey H. Johnson <trnsz@pobox.com>
 */

/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

/*
 * BUGS:
 *   - no handling of triple indirect blocks
 *   - does not set file access permissions, owner, group
 *   - no partition decoding implemented, offsets are hardcoded (STARTPART)
 */

#define DEBUG

#include <endian.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#undef short_t
#define short_t   int16_t

#undef ushort_t
#define ushort_t uint16_t

#undef of5_t
#ifdef off_t
# define of5_t      off_t
#else
# define of5_t   uint64_t
#endif /* ifdef off_t */

#undef time_t
#define time_t   uint32_t

int fd;
int outfd = 2;
int filesize = 0;

#define S5IFMT   0170000   /* type of file      */
#define S5IFDIR  0040000   /* directory         */
#define S5IFCHR  0020000   /* character special */
#define S5IFBLK  0060000   /* block special     */
#define S5IFREG  0100000   /* regular           */
#define S5IFLNK  0120000   /* symbolic link     */
#define S5IFSOCK 0140000   /* socket            */

#define S5ISUID  04000     /* set user id on execution         */
#define S5ISGID  02000     /* set group id on execution        */
#define S5ISVTX  01000     /* save swapped text even after use */
#define S5IREAD   0400     /* read, write, execute permissions */
#define S5IWRITE  0200
#define S5IEXEC   0100

#pragma pack(1)
struct dinode
{
  ushort_t di_mode;        /* mode and type of file   */
  short_t di_nlink;        /* number of links to file */
  ushort_t di_uid;         /* owner's user id         */
  ushort_t di_gid;         /* owner's group id        */
  of5_t di_size;           /* number of bytes in file */
  char di_addr[40];        /* disk block addresses    */

  /*
   * The last 3 chars of the disk block addresses and the extra byte
   * must be divided in the following structure so that SVS C will
   * correctly assign the proper bit fields for the extended mode
   * flags and the NFS file generation number. The address fields
   * are actually 39 characters long.
   */

  time_t di_atime;         /* time last accessed */
  time_t di_mtime;         /* time last modified */
  time_t di_ctime;         /* time created       */
};

#ifndef DIRSIZ
# define DIRSIZ 14
#endif /* ifndef DIRSIZ */
struct dirent
{
  ushort_t d_ino;
  char d_name[DIRSIZ];
};

uint8_t *STARTPART[] = {
  (uint8_t *)0x00000000
};

int partno = 0;

uint8_t *
ino2off(int inr)
{
  uint8_t *start = (uint8_t *)( STARTPART[partno] + 0x800 ); /* first inode */

  start = start + 64 * ( inr - 1 );
  return start;
}

void
dumpblk(uint32_t blkno)
{
  uint8_t *start = (uint8_t *)STARTPART[partno]; /* first block of fs */

  start = start + 0x400 * blkno;

  uint8_t buf[1024];

  lseek(fd, (of5_t)start, SEEK_SET);
  read(fd, buf, 1024);
  write(outfd, buf, ( filesize >= 1024 ) ? 1024 : filesize);
  filesize = filesize - 1024;

  /*
   * for (int i=0; i<1024; i++) {
   *            printf("%c", buf[i]);
   * }
   */

  printf("\n");
}

void recurse_inode(uint32_t, char *name);

void
lsdir(uint32_t blkno)
{
  uint8_t *start = (uint8_t *)STARTPART[partno]; /* first block of fs */

  start = start + 0x400 * blkno;

  struct dirent d;

  for (int i = 0; i < 1024; i = i + 16)
    {
      lseek(fd, (of5_t)start, SEEK_SET);
      read(fd, &d, sizeof ( struct dirent ));
      if (d.d_ino != 0)
        {
          printf(">>> %05d %s:\n", ntohs(d.d_ino), &d.d_name[0]);
          if (strcmp(d.d_name, ".") && strcmp(d.d_name, ".."))
            {
              recurse_inode(ntohs(d.d_ino), d.d_name);
            }
        }

      start += sizeof ( struct dirent );
    }
}

void
oneind(uint32_t blkno)
{
  uint8_t *start = (uint8_t *)STARTPART[partno]; /* first block of fs */

  start = start + 0x400 * blkno;

  uint8_t buf[1024];

  lseek(fd, (of5_t)start, SEEK_SET);
  read(fd, buf, 1024);

  for (int i = 0; i < 256; i++)
    {
      uint32_t blockno = ntohl(*(uint32_t *)( buf + 4 * i ));
      if (blockno != 0)
        {
#ifdef DEBUG
          printf("1ind blk %d\n", blockno);
#endif /* ifdef DEBUG */
          dumpblk(blockno);
        }
    }
}

void
twoind(uint32_t blkno)
{
  uint8_t *start = (uint8_t *)STARTPART[partno]; /* first block of fs */

  start = start + 0x400 * blkno;

  uint8_t buf[1024];

  lseek(fd, (of5_t)start, SEEK_SET);
  read(fd, buf, 1024);

  for (int i = 0; i < 256; i++)
    {
      uint32_t blockno = ntohl(*(uint32_t *)( buf + 4 * i ));
      if (blockno != 0)
        {
#ifdef DEBUG
          printf("2ind blk %d\n", blockno);
#endif /* ifdef DEBUG */
          oneind(blockno);
        }
    }
}

/* remove invalid characters from file name */
int
validopen(char *name)
{
  char buf[15]; /* 14 char max svr3 file name */

  strncpy(buf, name, 14);
  buf[14] = '\0';
  for (int i = 0; i < 14; i++)
    {
      if (buf[i] == '\0')
        {
          break;
        }

      if (( buf[i] < ' ' ) || ( buf[i] > 0x7e ) || buf[i] == '/')
        {
          buf[i] = '.';
        }
    }

  return open(buf, O_RDWR | O_CREAT, 0777);
}

void
recurse_inode(uint32_t inodeno, char *name)
{
  struct dinode ino;
  /*int inr;*/

  uint8_t *off = ino2off(inodeno);

  lseek(fd, (of5_t)off, SEEK_SET);
  read(fd, &ino, sizeof ( struct dinode ));

  printf("===============\n");
#ifdef DEBUG
  printf("off:  %s\n", off);
  printf("mode: %x\n", ntohs(ino.di_mode));

  if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFDIR)
    {
      printf("      dir\n");
    }

  if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFREG)
    {
      printf("      file\n");
    }

  if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFCHR)
    {
      printf("      char\n");
    }

  if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFBLK)
    {
      printf("      blk\n");
    }

  if (ntohs(ino.di_mode) & S5ISUID)
    {
      printf("      setuid\n");
    }

  if (ntohs(ino.di_mode) & S5ISGID)
    {
      printf("      setgid\n");
    }

  printf("      access permissions: %03o \n", ntohs(ino.di_mode) & 0777);

  printf("nlnk: %d\n", ntohs(ino.di_nlink));
  printf("uid:  %d\n", ntohs(ino.di_uid));
  printf("gid:  %d\n", ntohs(ino.di_gid));
  printf("size: %d\n", ntohl(ino.di_size));
  printf("atim: %d\n", ntohl(ino.di_atime));
  printf("mtim: %d\n", ntohl(ino.di_mtime));
  printf("ctim: %d\n", ntohl(ino.di_ctime));
#endif /* ifdef DEBUG */

  filesize = ntohl(ino.di_size);

  if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFDIR)
    {
      uint32_t blkno;
      printf("### Dir %s:\n", name);
      mkdir(name, 0777);
      chdir(name);
      for (int i = 0; i < 10; i++)
        {
          blkno = (uint8_t)ino.di_addr[3 * i] << 16;
          blkno += (uint8_t)ino.di_addr[3 * i + 1] << 8;
          blkno += (uint8_t)ino.di_addr[3 * i + 2];
          if (blkno != 0)
            {
#ifdef DEBUG
              printf("Block: %06x\n", blkno);
#endif /* ifdef DEBUG */
              lsdir(blkno);
            }
        }

      chdir("..");
      printf("\n");
    }
  else if (( ntohs(ino.di_mode) & S5IFMT ) == S5IFREG)
    {
      uint32_t blkno;
      outfd = validopen(name);
      printf("File %s\n", name);
#ifdef DEBUG
      printf("Blocks:\n");
#endif /* ifdef DEBUG */
      if (outfd < 0)
        {
          perror("create");
          exit(1);
        }

      for (int i = 0; i < 10; i++)
        {
          blkno = (uint8_t)ino.di_addr[3 * i] << 16;
          blkno += (uint8_t)ino.di_addr[3 * i + 1] << 8;
          blkno += (uint8_t)ino.di_addr[3 * i + 2];
          if (blkno != 0)
            {
#ifdef DEBUG
              printf("0x%06x ", blkno);
#endif /* ifdef DEBUG */
              dumpblk(blkno);
            }
        }

      printf("\n");

      blkno = (uint8_t)ino.di_addr[30] << 16;
      blkno += (uint8_t)ino.di_addr[31] << 8;
      blkno += (uint8_t)ino.di_addr[32];
      if (blkno != 0)
        {
#ifdef DEBUG
          printf("1x ind block: ");
          printf("%06x ", blkno);
          printf("\n");
#endif /* ifdef DEBUG */
          oneind(blkno);
        }

      blkno  = (uint8_t)ino.di_addr[33] << 16;
      blkno += (uint8_t)ino.di_addr[34] << 8;
      blkno += (uint8_t)ino.di_addr[35];
      if (blkno != 0)
        {
#ifdef DEBUG
          printf("2x ind block: ");
          printf("%06x ", blkno);
          printf("\n");
#endif /* ifdef DEBUG */
          twoind(blkno);
        }

      close(outfd);
    }
}

int
main(int argc, char **argv)
{
  if (argc != 4)
    {
      printf("s5fstool v0.0.1\n\n");
      printf("  Usage: %s filename partnr inodenr\n", argv[0]);
      exit(1);
    }

  partno = atoi(argv[2]);

  /* fd = open("../s1505_cp3540.dd", O_RDONLY); */
  fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    {
      perror("open");
      exit(1);
    }

  mkdir("dump", 0777);
  chdir("dump");

  recurse_inode(atoi(argv[3]), "root");
}
