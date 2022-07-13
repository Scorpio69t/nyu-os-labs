//
// Created by Xiao Ma on 11/29/21.
//
//https://blog.csdn.net/lell3538/article/details/59122211
//gcc -l crypto nyufile.c -std=c99 -o nyufile
//xxd -r dump disk
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h> //fstat
#include <sys/mman.h> //mmap
#include <fcntl.h>    //open
#include <string.h>   //strlen
#include <stdlib.h>   //exit
#include <stdint.h>
//#include <stddef.h>
#include <openssl/sha.h>  //sha1

#define SHA_DIGEST_LENGTH 20
//#include "nyufile.h"
char emptySHA1[SHA_DIGEST_LENGTH*2+1] = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
int combination[252][10] = {0};
int len[252] = {0};
int fileCluArr[9] = {0};
int notInclude[9] = {0};
int a[10] = {0};
int row = 0;
int UnContFoundFlag = 0;
int contFlag = 0;
int resultArr[9] = {0};
uint32_t FAT_EOF = 0x0fffffff;

#pragma pack(push,1) //not want to align the data structure
typedef struct BootEntry {
    unsigned char  BS_jmpBoot[3];     // Assembly instruction to jump to boot code
    unsigned char  BS_OEMName[8];     // OEM Name in ASCII
    unsigned short BPB_dBytsPerSec;    // Bytes per sector. Allowed values include 512, 1024, 2048, and 4096
    unsigned char  BPB_SecPerClus;    // Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB or smaller
    unsigned short BPB_RsvdSecCnt;    // Size in sectors of the reserved area
    unsigned char  BPB_NumFATs;       // Number of FATs
    unsigned short BPB_RootEntCnt;    // Maximum number of files in the root directory for FAT12 and FAT16. This is 0 for FAT32
    unsigned short BPB_TotSec16;      // 16-bit value of number of sectors in file system
    unsigned char  BPB_Media;         // Media type
    unsigned short BPB_FATSz16;       // 16-bit size in sectors of each FAT for FAT12 and FAT16. For FAT32, this field is 0
    unsigned short BPB_SecPerTrk;     // Sectors per track of storage device
    unsigned short BPB_NumHeads;      // Number of heads in storage device
    unsigned int   BPB_HiddSec;       // Number of sectors before the start of partition
    unsigned int   BPB_TotSec32;      // 32-bit value of number of sectors in file system. Either this value or the 16-bit value above must be 0
    unsigned int   BPB_FATSz32;       // 32-bit size in sectors of one FAT
    unsigned short BPB_ExtFlags;      // A flag for FAT
    unsigned short BPB_FSVer;         // The major and minor version number
    unsigned int   BPB_RootClus;      // Cluster where the root directory can be found
    unsigned short BPB_FSInfo;        // Sector where FSINFO structure can be found
    unsigned short BPB_BkBootSec;     // Sector where backup copy of boot sector is located
    unsigned char  BPB_Reserved[12];  // Reserved
    unsigned char  BS_DrvNum;         // BIOS INT13h drive number
    unsigned char  BS_Reserved1;      // Not used
    unsigned char  BS_BootSig;        // Extended boot signature to identify if the next three values are valid
    unsigned int   BS_VolID;          // Volume serial number
    unsigned char  BS_VolLab[11];     // Volume label in ASCII. User defines when creating the file system
    unsigned char  BS_FilSysType[8];  // File system type label in ASCII
} BootEntry;  //90 bytes
#pragma pack(pop)

#pragma pack(push,1)
typedef struct DirEntry {
    unsigned char  DIR_Name[11];      // File name
    unsigned char  DIR_Attr;          // File attributes
    unsigned char  DIR_NTRes;         // Reserved
    unsigned char  DIR_CrtTimeTenth;  // Created time (tenths of second)
    unsigned short DIR_CrtTime;       // Created time (hours, minutes, seconds)
    unsigned short DIR_CrtDate;       // Created day
    unsigned short DIR_LstAccDate;    // Accessed day
    unsigned short DIR_FstClusHI;     // High 2 bytes of the first cluster num(address)
    unsigned short DIR_WrtTime;       // Written time (hours, minutes, seconds
    unsigned short DIR_WrtDate;       // Written day
    unsigned short DIR_FstClusLO;     // Low 2 bytes of the first cluster address
    unsigned int   DIR_FileSize;      // File size in bytes. (0 for directories)
} DirEntry;  //32 bytes
#pragma pack(pop)

void printUsage() {
    printf("Usage: ./nyufile disk <options>\n");
    printf("  -i                     Print the file system information.\n");
    printf("  -l                     List the root directory.\n");
    printf("  -r filename [-s sha1]  Recover a contiguous file.\n");
    printf("  -R filename -s sha1    Recover a possibly non-contiguous file.\n");

}

char *mapDisk(int fd, size_t diskSize) {
    char *disk = mmap(NULL, diskSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return disk;
};

void printSysInfo(char *disk) {

    BootEntry *bootEntry = (BootEntry *) disk;

    printf("Number of FATs = %d\n"
           "Number of bytes per sector = %d\n"
           "Number of sectors per cluster = %d\n"
           "Number of reserved sectors = %d\n",
           bootEntry->BPB_NumFATs,bootEntry->BPB_dBytsPerSec,
           bootEntry->BPB_SecPerClus,bootEntry->BPB_RsvdSecCnt);
};

DirEntry *getEntry(char *disk,BootEntry *bootEntry,int clu) {
    return (DirEntry *) (disk + bootEntry->BPB_dBytsPerSec * (bootEntry->BPB_RsvdSecCnt
                         + bootEntry->BPB_NumFATs*bootEntry->BPB_FATSz32
                         + (clu-2)*bootEntry->BPB_SecPerClus));
}

void listRootDir(char *disk) {
    //find the location of root cluster (2)
    BootEntry *bootEntry = (BootEntry *) disk;
    DirEntry *dirEntry = getEntry(disk,bootEntry,bootEntry->BPB_RootClus);
//    row 1024:f8ff ff0f ffff ff0f f8ff ff0f ffff ff0f
//             ffff ff0f EOF>=0x0ffffff8
    uint8_t *fat1;
    uint32_t rootDirNextClu;
    int rootCluCnt = 0;
    int entryCount = 0;

    do {
        rootCluCnt++;

        int entryCntPerClu = bootEntry->BPB_dBytsPerSec*bootEntry->BPB_SecPerClus/32;
        for (int i = 0; i < entryCntPerClu; ++i) {
            //judge whether the next entry is empty
            if (*(dirEntry->DIR_Name) == 0x00)
                break;
            else if(*(dirEntry->DIR_Name) == 0xe5) {  //judge the deleted file
                dirEntry = dirEntry + 1;
                continue;
            }

            for (int j = 0; j < 8; ++j) {
                if (*(dirEntry->DIR_Name + j) != ' ') //&& *(dirEntry->DIR_Name + j) != '\n' && *(dirEntry->DIR_Name + j) != '\0')
                    printf("%c",*(dirEntry->DIR_Name + j));
                else
                    break;
            }

            //judge if it is the directory(append '/') / file(print extension)
            if ((dirEntry->DIR_Attr & 0x10) == 0x10)
                printf("/");
            else {
                int j = 0;
                while(dirEntry->DIR_Name[8+j] != ' ' && j<3) {//&& dirEntry->DIR_Name[8+j] != '\0' && dirEntry->DIR_Name[8+j] != '\n'&& j<3) {
                    if (j==0)
                        printf(".");
                    printf("%c", dirEntry->DIR_Name[8+j]);
                    j++;
                }
            }

            //calculate the high and low address, 2112(high) 3453(low) -> 1221 5334
            printf(" (size = %d, starting cluster = %d)\n",dirEntry->DIR_FileSize,
                   dirEntry->DIR_FstClusLO+dirEntry->DIR_FstClusHI*65536);

            dirEntry = dirEntry + 1;
            entryCount += 1;
        }
        //find the next cluster
        if (rootCluCnt>1) {
            fat1 = (uint8_t *)(disk + bootEntry->BPB_RsvdSecCnt*bootEntry->BPB_dBytsPerSec + rootDirNextClu*4);
            memcpy(&rootDirNextClu,fat1,4);
        }
        else if (rootCluCnt == 1) {
            fat1 = (uint8_t *)(disk + bootEntry->BPB_RsvdSecCnt*bootEntry->BPB_dBytsPerSec + bootEntry->BPB_RootClus*4);
            memcpy(&rootDirNextClu,fat1,4);        //copy the next cluster to rootDirNextClu
        }

        //find the location of dir entry of next cluster
        dirEntry = getEntry(disk,bootEntry,rootDirNextClu);

    }while (rootDirNextClu < 0x0ffffff8);
    printf("Total number of entries = %d\n",entryCount);

};

int getCluCnt(BootEntry *bootEntry,DirEntry *fileEntry) {
    return fileEntry->DIR_FileSize/(bootEntry->BPB_dBytsPerSec*bootEntry->BPB_SecPerClus) +
            ((fileEntry->DIR_FileSize % (bootEntry->BPB_dBytsPerSec*bootEntry->BPB_SecPerClus) == 0)?0:1);
}

int getCluBt(BootEntry *bootEntry) {
    return bootEntry->BPB_dBytsPerSec*bootEntry->BPB_SecPerClus;
}

int factorial(int i) {
    if (i==0 || i==1)
        return 1;
    return i * factorial(i-1);
}


void DFS(int notInArr[],int notInCnt, int total, int num,int i, int k){
    if(i ==(total+1))
        return;
    i++;
    if(notInCnt > 0) {
        for (int j = 0; j < notInCnt; ++j) {
            if (i == notInArr[j])
                i++;
        }
    }
    if(k==num) {
        len[row] = num;
        for (int l = 0; l < num; ++l) {
            combination[row][l] = a[l];
        }
        row++;
        return;
    }
    a[k] = i;

    DFS(notInArr,notInCnt,total,num,i,k+1);
    DFS(notInArr,notInCnt,total,num,i,k);

}

char* compareSha1(char *disk,BootEntry *bootEntry,char *fileContent,
                    int fileSize,int arr[], int n,char*mdString) {
    if (n>1) {
        for (int i = 0; i < n - 1; ++i)
            memcpy(fileContent+getCluBt(bootEntry)*(i+1), (char *) getEntry(disk, bootEntry, arr[i]), getCluBt(bootEntry));
    }
    memcpy(fileContent+getCluBt(bootEntry)*n, (char*)getEntry(disk,bootEntry,arr[n-1]),fileSize-getCluBt(bootEntry)*n);

    unsigned char digest[SHA_DIGEST_LENGTH];

    SHA_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, fileContent, fileSize);
    SHA1_Final(digest, &ctx);

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(&mdString[i * 2], "%02x", (unsigned int) digest[i]);
    return mdString;

}
void storeResult(int a[], int n)
{
    for (int i = 0; i < n; i++)
        resultArr[i] = a[i];
}

void swap(int* a, int* b) {
    int temp;
    temp = *a;
    *a = *b;
    *b = temp;
}

// Generating permutation using Heap Algorithm
void heapPermutation(char *disk,BootEntry *bootEntry,char *fileContent,
                     int fileSize,int a[], int size, int n,char* givenNum,char*mdString) {
    // if size becomes 1 then prints the obtained permutation
    if (size == 1) {
        storeResult(a, n);
        char *temp;
        temp = compareSha1(disk,bootEntry,fileContent,fileSize,a, n,mdString);
        if (strcmp(givenNum,temp)==0) {
            UnContFoundFlag = 1;
        }
        return;
    }
    for (int i = 0; i < size; i++) {
        if (UnContFoundFlag)
            return;
        heapPermutation(disk,bootEntry,fileContent,fileSize,a, size - 1, n,givenNum,mdString);
        // if size is odd, swap 0th i.e (first) and (size-1)th i.e (last) element
        if (size % 2 == 1)
            swap(&a[0], &a[size-1]);
            // If size is even, swap ith and (size-1)th i.e (last) element
        else
            swap(&a[i], &a[size - 1]);
    }

}

void recoverConFile(char *disk, size_t diskSize,char *fileName,char *givenNum) {
    BootEntry *bootEntry = (BootEntry *) disk;
    DirEntry *dirEntry = getEntry(disk,bootEntry,bootEntry->BPB_RootClus);

    uint32_t rootDirNextClu,fileStartClu,fileNextClu;
    int rootCluCnt = 0;
    int isFind = 0;             //when find the file, isFind = 1;
    int isFindSha = 0;          //when find the file, isFindSha = 1;
    int countFindFile = 0;
    DirEntry *fileEntry;    //record the dirEntry location of the file

    int nameLen = 0,match = 0;
    while(*(fileName+nameLen)) {
        nameLen++;
    }
    do {
        rootCluCnt++;

        int entryCntPerClu = bootEntry->BPB_dBytsPerSec*bootEntry->BPB_SecPerClus/32;
        for (int i = 0; i < entryCntPerClu; ++i) {
            if (*(dirEntry->DIR_Name) == 0x00) //if
                break;
            if (dirEntry->DIR_Attr == 0x10) {  //if is dir
                dirEntry = dirEntry + 1;
                continue;
            }
//            printf("nameLen = %d\n",nameLen);
            if (dirEntry->DIR_Name[0] == 0xe5) {
                for (int k = 1, j = 1; k < 11; ++k) {
                    if (*(fileName + j) != '.') {
                        if (j==nameLen && dirEntry->DIR_Name[8]==' ') {
                            match = 1;
                            k = 10;
                        }
//                        printf("--filename[%d] = %c,dirname = %c\n",j,fileName[j],dirEntry->DIR_Name[k]);
                        if (dirEntry->DIR_Name[k] == fileName[j++] || match == 1) {
//                            printf("filename[%d] = %c,dirname = %c\n",j-1,fileName[j-1],dirEntry->DIR_Name[k]);
                            if (k == 10) {
                                countFindFile++;
                                fileEntry = dirEntry;
                                if (givenNum != NULL) {
                                    char *mdString = malloc(sizeof(char) * SHA_DIGEST_LENGTH * 2 + 1);
                                    //empty file
                                    if (fileEntry->DIR_FileSize == 0)
                                        memcpy(mdString,emptySHA1,SHA_DIGEST_LENGTH * 2 + 1);
                                    else if (contFlag){
                                        char *fileContent = malloc(sizeof(char) * (fileEntry->DIR_FileSize));
                                        unsigned char digest[SHA_DIGEST_LENGTH];
                                        fileStartClu = (dirEntry->DIR_FstClusLO + dirEntry->DIR_FstClusHI * 65536);

                                        memcpy(fileContent, getEntry(disk,bootEntry,fileStartClu),fileEntry->DIR_FileSize);

                                        SHA_CTX ctx;
                                        SHA1_Init(&ctx);
                                        SHA1_Update(&ctx, fileContent, fileEntry->DIR_FileSize);
                                        SHA1_Final(digest, &ctx);

                                        for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
                                            sprintf(&mdString[i * 2], "%02x", (unsigned int) digest[i]);
                                    }
                                    else {
                                        fileStartClu = (dirEntry->DIR_FstClusLO + dirEntry->DIR_FstClusHI * 65536);
                                        int cluNum = getCluCnt(bootEntry,fileEntry);

                                        uint32_t fatValue = 0;
                                        int fileCluCnt = 0;
                                        int notInCnt = 0;
                                        for (int l = 0; l < 10; ++l) {
                                            memcpy(&fatValue, (disk + bootEntry->BPB_RsvdSecCnt * bootEntry->BPB_dBytsPerSec + (2+l) * 4),4);     //copy the value of rootDirNextClu in FAT

                                            if (fatValue == 0x00000000 && getEntry(disk,bootEntry,l+2)->DIR_Name[0] !=0x00) {
                                                if ((l+2) ==fileStartClu) {
                                                    notInclude[notInCnt++] = l+2;
                                                    continue;
                                                }
                                                else
                                                    fileCluArr[fileCluCnt++] = l+2;
                                            }
                                            else {
                                                notInclude[notInCnt++] = l+2;
                                            }
                                        }
                                        char *fileContent = malloc(sizeof(char) * (fileEntry->DIR_FileSize));
                                        memcpy(fileContent, getEntry(disk,bootEntry,fileStartClu),getCluBt(bootEntry));

                                        DFS(notInclude,notInCnt,11,cluNum-1,1,0);

                                        for (int g = 0; g < row; ++g) {
                                            if (UnContFoundFlag)
                                                break;
                                            heapPermutation(disk,bootEntry,fileContent,fileEntry->DIR_FileSize,
                                                            combination[g],len[g],len[g],givenNum,mdString);
                                        }
                                    }

                                    if (strcmp(givenNum, mdString) == 0 || UnContFoundFlag == 1) {
                                        isFind = 1;
                                        isFindSha = 1;
                                        break;
                                    } else
                                        continue;
                                } else {
                                    isFind = 1;
                                    fileStartClu = (dirEntry->DIR_FstClusLO + dirEntry->DIR_FstClusHI * 65536);
                                }
                            } else
                                continue;
                        } else
                            break;
                    } else {
                        if (k==8 ||(k <= 7 && dirEntry->DIR_Name[k] == ' ')) {
                            j++;
                            k = 7;
                        } else
                            break;
                    }
                }
            }
            if (isFindSha)
                break;
            dirEntry = dirEntry + 1;
        }
        if (*(dirEntry->DIR_Name) == 0x00)
            break;
        if (isFindSha)
            break;
        if (rootCluCnt>1)       //find the next cluster
            memcpy(&rootDirNextClu, (disk + bootEntry->BPB_RsvdSecCnt * bootEntry->BPB_dBytsPerSec + rootDirNextClu * 4),4);     //copy the value of rootDirNextClu in FAT
        else if (rootCluCnt == 1)
            memcpy(&rootDirNextClu,(disk + bootEntry->BPB_RsvdSecCnt * bootEntry->BPB_dBytsPerSec + bootEntry->BPB_RootClus * 4),4);   //copy the next cluster to rootDirNextClu

        //find the location of dir entry of next cluster
        dirEntry = getEntry(disk,bootEntry,rootDirNextClu);

    }while (rootDirNextClu < 0x0ffffff8);

    //if not find the file
    if (isFind == 0) {
        printf("%s: file not found\n", fileName);
        exit(0);
    }
    if (countFindFile > 1 && isFindSha == 0) {
        printf("%s: multiple candidates found\n",fileName);
        exit(0);
    }

//    recover the file
    *(fileEntry->DIR_Name) = fileName[0];       //change first letter of the name

    int fileCluCount = getCluCnt(bootEntry,fileEntry);

    //change FAT1 and FAT2
    if (fileCluCount == 0 && isFindSha == 1) {          //empty file
        printf("%s: successfully recovered with SHA-1\n",fileName);
        exit(0);
    }
    else if (fileCluCount == 0) {
        printf("%s: successfully recovered\n", fileName);
        exit(0);
    }

    //not empty file
    if (UnContFoundFlag) {      //not continue file
        for (int l = 0; l < bootEntry->BPB_NumFATs; ++l)
            memcpy((disk + (bootEntry->BPB_RsvdSecCnt + l*bootEntry->BPB_FATSz32)*bootEntry->BPB_dBytsPerSec + (fileStartClu)*4),&resultArr[0],4);
        for (int h = 1; h < fileCluCount-1; ++h) {
            for (int l = 0; l < bootEntry->BPB_NumFATs; ++l) {
                memcpy((disk + (bootEntry->BPB_RsvdSecCnt + l*bootEntry->BPB_FATSz32)*bootEntry->BPB_dBytsPerSec + resultArr[h-1]*4),&resultArr[h],4);
            }
        }
        for (int e = 0; e < bootEntry->BPB_NumFATs; ++e) {
            memcpy((disk + (bootEntry->BPB_RsvdSecCnt + e*bootEntry->BPB_FATSz32)*bootEntry->BPB_dBytsPerSec + (resultArr[fileCluCount-2])*4),&FAT_EOF,4);
        }
    }else {                     //continue file / regular file
        for (int h = 1; h < fileCluCount; ++h) {
            uint32_t buf = fileStartClu + h;
            for (int l = 0; l < bootEntry->BPB_NumFATs; ++l) {
                memcpy((disk + (bootEntry->BPB_RsvdSecCnt + l * bootEntry->BPB_FATSz32) * bootEntry->BPB_dBytsPerSec +
                        (fileStartClu + h - 1) * 4), &buf, 4);
            }
        }
        for (int e = 0; e < bootEntry->BPB_NumFATs; ++e) {
            memcpy((disk + (bootEntry->BPB_RsvdSecCnt + e * bootEntry->BPB_FATSz32) * bootEntry->BPB_dBytsPerSec +
                    (fileStartClu + fileCluCount - 1) * 4), &FAT_EOF, 4);
        }
    }

    msync(disk, diskSize,MS_SYNC);

    if (isFindSha)
        printf("%s: successfully recovered with SHA-1\n",fileName);
    else {
        if (isFind)
            printf("%s: successfully recovered\n", fileName);
    }
};

int main(int argc,char* argv[]) {
    char *disk;
    int fd;
    struct stat s;
    if (argc < 3) {
        printUsage();
    }
    else {
        if ((fd = open(argv[1],O_RDWR)) == -1 ) {
            fprintf(stderr, "Error: invalid disk\n");
            exit(-1);
        }
        fstat(fd, &s);
        disk = mapDisk(fd,s.st_size);

        int flag = 0;
        if (argc == 3) {
            if (strcmp(argv[2], "-i") == 0) {
                printSysInfo(disk);
                flag = 1;
            } else if (strcmp(argv[2], "-l") == 0) {
                listRootDir(disk);
                flag = 1;
            }
        }else if (argc == 4) {
            if (strcmp(argv[2],"-r") == 0) {
                recoverConFile(disk,s.st_size,argv[3],NULL);
                flag = 1;
            }
        }else if (argc == 6) {
            if ((strcmp(argv[2],"-r") == 0 && strcmp(argv[4],"-s") == 0)) {
                contFlag = 1;
                recoverConFile(disk,s.st_size,argv[3],argv[5]);
                flag = 1;
            }
            else if ((strcmp(argv[2],"-s") == 0 && strcmp(argv[4],"-s") == 0)){
                contFlag = 1;
                recoverConFile(disk,s.st_size,argv[5],argv[3]);
                flag = 1;
            }else if ((strcmp(argv[2],"-R") == 0 && strcmp(argv[4],"-s") == 0)){
                recoverConFile(disk,s.st_size,argv[3],argv[5]);

                flag = 1;
            }else if ((strcmp(argv[2],"-s") == 0 && strcmp(argv[4],"-R") == 0)){
                recoverConFile(disk,s.st_size,argv[5],argv[3]);
                flag = 1;
            }
        }

        if (flag == 0) {
            printUsage();
        }
    }
    return 0;
}