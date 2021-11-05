//
// Created by macskas on 12/9/20.
//

#include "nsca_common.h"
#include "nsca_utils.h"
#include <mcrypt.h>

#include <cstring>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <cstdio>

static unsigned long crc32_table[256];
static volatile sig_atomic_t mcrypt_initialized=FALSE;
static unsigned long local_rnd_x = 123456789, local_rnd_y=362436069, local_rnd_z = 521288629;

void generate_crc32_table(){
    unsigned long crc, poly;
    int i, j;

    poly=0xEDB88320L;
    for(i=0;i<256;i++){
        crc=i;
        for(j=8;j>0;j--){
            if(crc & 1)
                crc=(crc>>1)^poly;
            else
                crc>>=1;
        }
        crc32_table[i]=crc;
    }
}

/* calculates the CRC 32 value for a buffer */
unsigned long calculate_crc32(const char *buffer, int buffer_size){
    if (buffer == nullptr)
        return 0;

    register unsigned long crc = 0xFFFFFFFF;
    int this_char;
    int current_index;


    for(current_index=0; current_index<buffer_size; current_index++){
        this_char=(int)buffer[current_index];
        crc=((crc>>8) & 0x00FFFFFF) ^ crc32_table[(crc ^ this_char) & 0xFF];
    }

    return (crc ^ 0xFFFFFFFF);
}

unsigned long xorshf96() {          //period 2^96-1
    unsigned long t;
    local_rnd_x ^= local_rnd_x << 16;
    local_rnd_x ^= local_rnd_x >> 5;
    local_rnd_x ^= local_rnd_x << 1;

    t = local_rnd_x;
    local_rnd_x = local_rnd_y;
    local_rnd_y = local_rnd_z;
    local_rnd_z = t ^ local_rnd_x ^ local_rnd_y;

    return local_rnd_z;
}

/* initializes encryption routines */
int encrypt_init(const char *password,int encryption_method,char *received_iv,struct crypt_instance **CIptr){

    int i;
    int iv_size;

    struct crypt_instance *CI;

    CI=(crypt_instance *)(malloc(sizeof(struct crypt_instance)));
    *CIptr=CI;

    if(CI== nullptr){
        return ERROR;
    }

    /* server generates IV used for encryption */
    if(received_iv==nullptr)
        generate_transmitted_iv(CI->transmitted_iv);

        /* client receives IV from server */
    else
        memcpy(CI->transmitted_iv,received_iv,TRANSMITTED_IV_SIZE);

    CI->iv_size = 0;
    CI->blocksize=1;                        /* block size = 1 byte w/ CFB mode */
    CI->keysize=7;                          /* default to 56 bit key length */
    CI->mcrypt_mode = nullptr;
    CI->mcrypt_algorithm = nullptr;

    /* XOR or no encryption */
    if(encryption_method==ENCRYPT_NONE || encryption_method==ENCRYPT_XOR)
        return OK;

    CI->mcrypt_mode = (char*)malloc(64);                  /* CFB = 8-bit cipher-feedback mode */
    CI->mcrypt_algorithm = (char*)malloc(128);

    memset(CI->mcrypt_mode, 0, 64);
    memset(CI->mcrypt_algorithm, 0, 128);

    strcpy(CI->mcrypt_mode, "cfb");
    strcpy(CI->mcrypt_algorithm, "unknown");

    /* get the name of the mcrypt encryption algorithm to use */
    switch(encryption_method){
        case ENCRYPT_DES:
            strcpy(CI->mcrypt_algorithm, MCRYPT_DES);
            break;
        case ENCRYPT_3DES:
            strcpy(CI->mcrypt_algorithm, MCRYPT_3DES);
            break;
        case ENCRYPT_CAST128:
            strcpy(CI->mcrypt_algorithm, MCRYPT_CAST_128);
            break;
        case ENCRYPT_CAST256:
            strcpy(CI->mcrypt_algorithm, MCRYPT_CAST_256);
            break;
        case ENCRYPT_XTEA:
            strcpy(CI->mcrypt_algorithm, MCRYPT_XTEA);
            break;
        case ENCRYPT_3WAY:
            strcpy(CI->mcrypt_algorithm, MCRYPT_3WAY);
            break;
        case ENCRYPT_BLOWFISH:
            strcpy(CI->mcrypt_algorithm, MCRYPT_BLOWFISH);
            break;
        case ENCRYPT_TWOFISH:
            strcpy(CI->mcrypt_algorithm, MCRYPT_TWOFISH);
            break;
        case ENCRYPT_LOKI97:
            strcpy(CI->mcrypt_algorithm, MCRYPT_LOKI97);
            break;
        case ENCRYPT_RC2:
            strcpy(CI->mcrypt_algorithm, MCRYPT_RC2);
            break;
        case ENCRYPT_ARCFOUR:
            strcpy(CI->mcrypt_algorithm, MCRYPT_ARCFOUR);
            break;
        case ENCRYPT_RIJNDAEL128:
            strcpy(CI->mcrypt_algorithm, MCRYPT_RIJNDAEL_128);
            break;
        case ENCRYPT_RIJNDAEL192:
            strcpy(CI->mcrypt_algorithm, MCRYPT_RIJNDAEL_192);
            break;
        case ENCRYPT_RIJNDAEL256:
            strcpy(CI->mcrypt_algorithm, MCRYPT_RIJNDAEL_256);
            break;
        case ENCRYPT_WAKE:
            strcpy(CI->mcrypt_algorithm, MCRYPT_WAKE);
            break;
        case ENCRYPT_SERPENT:
            strcpy(CI->mcrypt_algorithm, MCRYPT_SERPENT);
            break;
        case ENCRYPT_ENIGMA:
            strcpy(CI->mcrypt_algorithm, MCRYPT_ENIGMA);
            break;
        case ENCRYPT_GOST:
            strcpy(CI->mcrypt_algorithm, MCRYPT_GOST);
            break;
        case ENCRYPT_SAFER64:
            strcpy(CI->mcrypt_algorithm, MCRYPT_SAFER_SK64);
            break;
        case ENCRYPT_SAFER128:
            strcpy(CI->mcrypt_algorithm, MCRYPT_SAFER_SK128);
            break;
        case ENCRYPT_SAFERPLUS:
            strcpy(CI->mcrypt_algorithm, MCRYPT_SAFERPLUS);
            break;
        default:
            strcpy(CI->mcrypt_algorithm, "unknown");
            break;
    }
    /* open encryption module */
    if((CI->td=mcrypt_module_open(CI->mcrypt_algorithm, nullptr,CI->mcrypt_mode, nullptr))==MCRYPT_FAILED){
        //syslog(LOG_ERR,"Could not open mcrypt algorithm '%s' with mode '%s'",CI->mcrypt_algorithm,CI->mcrypt_mode);
        return ERROR;
    }

    /* determine size of IV buffer for this algorithm */
    iv_size=mcrypt_enc_get_iv_size(CI->td);
    if(iv_size>TRANSMITTED_IV_SIZE){
        //syslog(LOG_ERR,"IV size for crypto algorithm exceeds limits");
        return ERROR;
    }
    CI->iv_size = iv_size;

    /* allocate memory for IV buffer */
    if((CI->IV=(char *)malloc(iv_size))== nullptr){
        //syslog(LOG_ERR,"Could not allocate memory for IV buffer");
        return ERROR;
    }

    /* fill IV buffer with first bytes of IV that is going to be used to crypt (determined by server) */
    for(i=0;i<iv_size;i++)
        CI->IV[i]=CI->transmitted_iv[i];

    /* get maximum key size for this algorithm */
    CI->keysize=mcrypt_enc_get_key_size(CI->td);

    /* generate an encryption/description key using the password */
    if((CI->key=(char *)malloc(CI->keysize))== nullptr){
        //syslog(LOG_ERR,"Could not allocate memory for encryption/decryption key");
        return ERROR;
    }
    bzero(CI->key,CI->keysize);
    if((size_t)CI->keysize < strlen(password))
        strncpy(CI->key,password,CI->keysize);
    else
        strncpy(CI->key,password,strlen(password));

    /* initialize encryption buffers */
    mcrypt_generic_init(CI->td,CI->key,CI->keysize,CI->IV);
    mcrypt_initialized=TRUE;
    return OK;
}
/* encryption routine cleanup */
void encrypt_cleanup(int encryption_method, struct crypt_instance *CI){

    /* no crypt instance */
    if(CI==nullptr)
        return;

    /* mcrypt cleanup */
    if(encryption_method!=ENCRYPT_NONE && encryption_method!=ENCRYPT_XOR){
        if(mcrypt_initialized==TRUE)
            mcrypt_generic_end(CI->td);
        free(CI->key);
        CI->key = nullptr;
        free(CI->IV);
        CI->IV = nullptr;

        free(CI->mcrypt_algorithm);
        CI->mcrypt_algorithm = nullptr;

        free(CI->mcrypt_mode);
        CI->mcrypt_mode = nullptr;
    }

    free(CI);
}

void generate_transmitted_iv(char *transmitted_iv){
    for(int x=0;x<TRANSMITTED_IV_SIZE;x++)
        transmitted_iv[x]= (char)(xorshf96() & 0xff);
}

void generate_transmitted_iv_secure(char *transmitted_iv){
    FILE *fp;
    int x;
    int seed=0;

    /*********************************************************/
    /* fill IV buffer with data that's as random as possible */
    /*********************************************************/

    /* try to get seed value from /dev/urandom, as its a better source of entropy */
    fp=fopen("/dev/urandom","r");
    if(fp!= nullptr){
        seed=fgetc(fp);
        fclose(fp);
    }

        /* else fallback to using the current time as the seed */
    else
        seed=(int)time(nullptr);

    /* generate pseudo-random IV */
    srand(seed);
    for(x=0;x<TRANSMITTED_IV_SIZE;x++)
        transmitted_iv[x]=(char)((int)((256.0*rand())/(RAND_MAX+1.0)) & 0xff);
}



void encrypt_buffer(char *buffer,int buffer_size, const char *password, int encryption_method, struct crypt_instance *CI){
    int x;
    int y;
    int password_length;

    /* no crypt instance */
    if(CI == nullptr)
        return;

    /* no encryption */
    if(encryption_method==ENCRYPT_NONE)
        return;

        /* simple XOR "encryption" - not meant for any real security, just obfuscates data, but its fast... */
    else if(encryption_method==ENCRYPT_XOR){

        /* rotate over IV we received from the server... */
        for(y=0,x=0;y<buffer_size;y++,x++){

            /* keep rotating over IV */
            if(x>=TRANSMITTED_IV_SIZE)
                x=0;

            buffer[y]^=CI->transmitted_iv[x];
        }

        /* rotate over password... */
        password_length=strlen(password);
        for(y=0,x=0;y<buffer_size;y++,x++){

            /* keep rotating over password */
            if(x>=password_length)
                x=0;

            buffer[y]^=password[x];
        }

        return;
    }

    else{

        /* encrypt each byte of buffer, one byte at a time (CFB mode) */
        for(x=0;x<buffer_size;x++)
            mcrypt_generic(CI->td,&buffer[x],1);
    }
}



void decrypt_buffer(char *buffer,int buffer_size, const char *password, int encryption_method, struct crypt_instance *CI){
    int x=0;
    /* no crypt instance */
    if(CI== nullptr)
        return;

    /* no encryption */
    if(encryption_method == ENCRYPT_NONE)
        return;

        /* XOR "decryption" is the same as encryption */
    else if(encryption_method==ENCRYPT_XOR)
        encrypt_buffer(buffer,buffer_size,password,encryption_method,CI);

    else{
        //mdecrypt_generic(CI->td, buffer, buffer_size);
        /* encrypt each byte of buffer, one byte at a time (CFB mode) */
        for(x=0;x<buffer_size;x++)
            mdecrypt_generic(CI->td,&buffer[x],1);
    }

}
