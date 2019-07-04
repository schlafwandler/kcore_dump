#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <errno.h>
#include <elf.h>

#include "lime.h"

// #############################################################################
// ###                                                                       ###
// ###              THIS TOOL HAS NOT UNDERGONE MUCH TESTING!                ###
// ###                      USE IT AT YOUR OWN RISK!                         ###
// ###                                                                       ###
// #############################################################################

// Compile with gcc -o kcore_dump kcore_dump.c

#define KCORE_FILENAME "/proc/kcore"
#define IOMEM_FILENAME "/proc/iomem"
#define MAX_PHYS_RANGES 32
#define CHUNK_SIZE      0x100000    // 1M

struct addr_range
{
    int         index;
    uint64_t    start;
    uint64_t    end;
};

// Note: section here != section in ELF
struct section
{
    uint64_t    phys_base;
    uint64_t    file_offset;
    size_t      size;
};

int get_system_ram_addrs(struct addr_range *addrs);
int match_phdrs(Elf64_Phdr *prog_hdr, unsigned int num_hdrs, struct addr_range *ranges, unsigned int num_phys_ranges, struct section *sections);
int write_lime(int kcore_fd,int out_fd,struct section *sections,int num_ranges);
int copy_loop(int out_fd, int in_fd, size_t len);

int main(int argc, char *argv[])
{
    int fd,out_fd;

    if (argc < 2)
    {
        printf("Usage: %s <outfile>\n",argv[0]);
        return -1;
    }
    char *outfile_name = argv[1];

    if ((fd = open64(KCORE_FILENAME,O_RDWR|O_LARGEFILE)) == -1)
    {
        fprintf(stderr,"Could not open %s\n",KCORE_FILENAME);
        return -1;
    }

    struct addr_range ranges[MAX_PHYS_RANGES];
    int num_phys_ranges = get_system_ram_addrs(ranges);

    // get ELF header from kcore
    Elf64_Ehdr elf_hdr;
    read(fd,(void*)&elf_hdr,sizeof(elf_hdr));

    // get program headers
    lseek(fd,elf_hdr.e_phoff,SEEK_SET);
    size_t phdrs_size = elf_hdr.e_phnum*elf_hdr.e_phentsize;
    Elf64_Phdr *prog_hdr = (Elf64_Phdr*)malloc(phdrs_size);
    if (prog_hdr == NULL) {return -1;} // not expected
    read(fd,(void*)prog_hdr,phdrs_size);
     
    struct section sections[MAX_PHYS_RANGES];
    match_phdrs(prog_hdr,elf_hdr.e_phnum,ranges,num_phys_ranges,sections);

    // open output file
    if ((out_fd = open64(outfile_name,O_WRONLY|O_CREAT|O_LARGEFILE,S_IRUSR)) == -1)
    {
        fprintf(stderr,"Could not open %s\n",outfile_name);
        return -1;
    }

    // write output file
    write_lime(fd,out_fd,sections,num_phys_ranges);

    // cleanup
    close(out_fd);
    close(fd);
}

/*
 * write_lime
 *
 * Params:
 *  - int kcore_fd                  fd to /proc/kcore
 *  - int out_fd                    fd to output file
 *  - struct section *sections      array of section structs to dump
 *  - int num_ranges                number of section structs
 *
 * Dumps the memory regions described by the section structs to a 
 * .lime file.
 *
 * returns: 0 if OK, -1 if error
 */
int write_lime(int kcore_fd,int out_fd,struct section *sections,int num_ranges)
{
    lime_mem_range_header lime_header;
    lime_header.magic = 0x4C694D45;
    lime_header.version = 1;
    memset(&lime_header.reserved,0x00,8);

    for (int i=0;i<num_ranges;i++)
    {
        lime_header.s_addr = sections[i].phys_base;
        lime_header.e_addr = sections[i].phys_base + sections[i].size -1;

        // write lime_mem_range_header
        if (write(out_fd,&lime_header,sizeof(lime_mem_range_header)) != sizeof(lime_mem_range_header))
            {printf("Error writing file header (errno %d), aborting\n", errno); return -1;}

        printf("Copying section %d (0x%llx - 0x%llx)\n",i,lime_header.s_addr,lime_header.e_addr);

        // copy content over
        off64_t pos = lseek64(kcore_fd, sections[i].file_offset, SEEK_SET);
        if (pos == -1)
            {printf("Error setting positon in kcore (errno %d), aborting\n", errno); return -1;}

        if (copy_loop(out_fd,kcore_fd,sections[i].size) != 0)
            {printf("Error copying data (errno %d), aborting\n", errno); return -1;}
    }

    return 0;
}


/* 
 * copy_loop
 *
 * Params:
 *  - int out_fd
 *  - int in_fd
 *  - size_t len
 *
 * Copy len bytes from in_fd to out_fd
 * Expects both fd's to be lseek'ed to the right position
 * I tried using sendfile64 for better performance, but ran into problems
 *
 * returns: 0 if OK, -1 if error
 */
int copy_loop(int out_fd, int in_fd, size_t len)
{
    size_t  to_write = len;
    size_t  next_chunk;
    int have_read, written;
    char *buffer = malloc(CHUNK_SIZE);
    if (buffer == NULL) // not expected
        {return -1;}

    while (to_write)
    {
        if (to_write > CHUNK_SIZE)
            next_chunk = CHUNK_SIZE;
        else
            next_chunk = to_write;

        have_read = read(in_fd,buffer,next_chunk);
        if (have_read == -1)
            {free(buffer); return -1;}

        written = write(out_fd,buffer,have_read); 
        if (written == -1)
            {free(buffer); return -1;}

        to_write -= written;
    }

    free(buffer);
    return 0;
}

/*
 * match_phdrs
 *
 * Params:
 *  - Elf64_Phdr *prog_hdr              array of kcore program headers
 *  - unsigned int num_hdrs             number of kcore program headers
 *  - struct addr_range *ranges         array of addr_range structs from iomem
 *  - unsigned int num_phys_ranges      number of addr_range structs from iomem
 *  - struct section *sections          array of sections structs (OUTPUT)
 *
 * match_phdrs tries to match the address ranges from iomem with the segments (program
 * headers) from kcore.
 * Uses a guessed mapping base 
 * Fills a section struct for each match.
 *
 * returns: number of sections matched
 */
int match_phdrs(    Elf64_Phdr *prog_hdr, 
                    unsigned int num_hdrs, 
                    struct addr_range *ranges, 
                    unsigned int num_phys_ranges, 
                    struct section *sections)
{
    int sections_filled_in = 0;

    for (int i=0;i<num_hdrs;i++)
    {
        for (int j=0;j<num_phys_ranges;j++)
        {
            if (prog_hdr[i].p_paddr == ranges[j].start)
            {
                sections[sections_filled_in].phys_base = ranges[j].start;
                sections[sections_filled_in].file_offset = prog_hdr[i].p_offset;
                sections[sections_filled_in].size = prog_hdr[i].p_memsz;

                sections_filled_in++;
            }
        }
    }

    return sections_filled_in;
}

/*
 * get_system_ram_addrs
 *
 * Params:
 *  - struct addr_range *addrs          array of addr_range for output
 *
 * Greps /proc/iomem for lines containing "System RAM",
 * for each range: outputs index,start,end to given addr_range structs
 * 
 * returns: number of found ranges
 */
int get_system_ram_addrs(struct addr_range *addrs)
{
    FILE *fd;
    char *lineptr = malloc(512);
    size_t n = 512;
    int count = 0;
    
    if((fd = fopen(IOMEM_FILENAME,"r")) == NULL)
    {
        fprintf(stderr,"Could not open %s\n",IOMEM_FILENAME);
        exit(-1);
    }

    int index = 0;
    while(getline(&lineptr,&n,fd) != -1)
    {
        if(strstr(lineptr,"System RAM"))
        {
            uint64_t start;
            uint64_t end;
            // 00100000-dab0efff : System RAM
            sscanf(lineptr,"%lx-%lx",&start,&end);
            //// debug
            //printf("start: %9lx\tend: %9lx\tsize: %lx\n",start,end,end-start);

            addrs[count].index = index;
            addrs[count].start = start;
            addrs[count].end = end;

            if (++count >= MAX_PHYS_RANGES) {
                fprintf(stderr,"Too many physical ranges\n");
                exit(-1);
            }
        }

        if (lineptr[0] != ' ')
            index++;
    }

    fclose(fd);
    free(lineptr);

    return count;
}
