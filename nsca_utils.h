//
// Created by macskas on 12/9/20.
//

#ifndef NSCA_NSCA_UTILS_H
#define NSCA_NSCA_UTILS_H

#include <mcrypt.h>
#include "nsca_common.h"

struct crypt_instance {
        char transmitted_iv[TRANSMITTED_IV_SIZE];
        MCRYPT td;
        char *key;
        char *IV;
        char block_buffer;
        int blocksize;
        int keysize;
        char *mcrypt_algorithm;
        char *mcrypt_mode;
        int iv_size;
};

void generate_crc32_table();
unsigned long calculate_crc32(const char *, int);

int encrypt_init(const char *,int,char *,struct crypt_instance **);
void encrypt_cleanup(int,struct crypt_instance *);

void encrypt_buffer(char *,int,const char *,int,struct crypt_instance *);
void decrypt_buffer(char *,int,const char *,int,struct crypt_instance *);

void generate_transmitted_iv(char *);
void generate_transmitted_iv_secure(char *);

#endif //NSCA_NSCA_UTILS_H
