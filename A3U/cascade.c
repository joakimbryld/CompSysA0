#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <math.h>

#ifdef __APPLE__
#include "./endian.h"
#else
#include <endian.h>
#endif

#include "./cascade.h"
#include "./sha256.h"

char tracker_ip[IP_LEN];
char tracker_port[PORT_LEN];
char my_ip[IP_LEN];
char my_port[PORT_LEN];

struct csc_file *casc_file;
csc_block_t** queue;
csc_peer_t* peers;

/*
 * Frees global resources that are malloc'ed during peer downloads. 
 */
void free_resources()
{
    free(queue);
    free(peers);
    csc_free_file(casc_file);
}

/*
 * Gets a sha256 hash of a specified file, sourcefile. The hash itself is 
 * placed into the given variable 'hash'. Any size can be created, but a
 * a normal size for the hash would be given by the global variable 
 * 'SHA256_HASH_SIZE', that has been defined in sha256.h
 */
void get_file_sha(const char* sourcefile, char* hash, int size)
{
    int casc_file_size;

    FILE* fp = fopen(sourcefile, "rb");
    if (fp == 0)
    {
        printf("Failed to open source: %s\n", sourcefile);
        return;
    }
    
    fseek(fp, 0L, SEEK_END);
    casc_file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    char buffer[casc_file_size];
    fread(buffer, casc_file_size, 1, fp); 
    fclose(fp);
    
    get_data_sha(buffer, hash, casc_file_size, size);
}

/*
 * Gets a sha256 hash of specified data, sourcedata. The hash itself is 
 * placed into the given variable 'hash'. Any size can be created, but a
 * a normal size for the hash would be given by the global variable 
 * 'SHA256_HASH_SIZE', that has been defined in sha256.h
 */
void get_data_sha(const char* sourcedata, char* hash, uint32_t data_size, int hash_size)
{    
    SHA256_CTX shactx;
    unsigned char shabuffer[hash_size];
    sha256_init(&shactx);
    sha256_update(&shactx, sourcedata, data_size);
    sha256_final(&shactx, &shabuffer);
    
    for (int i=0; i<hash_size; i++)
    {
        hash[i] = shabuffer[i];
    }
}

/*
 * Perform all client based interactions in the P2P network for a given cascade file.
 * E.g. parse a cascade file and get all the relevent data from somewhere else on the 
 * network.
 */
void download_only_peer(char *cascade_file)
{
    printf("Managing download only for: %s\n", cascade_file);    
    if (access(cascade_file, F_OK ) != 0 ) 
    {
        fprintf(stderr, ">> File %s does not exist\n", cascade_file);
        exit(EXIT_FAILURE);
    }

    char output_file[strlen(cascade_file)]; 
    memcpy(output_file, cascade_file, strlen(cascade_file));
    char* r = strstr(cascade_file, "cascade");
    int cutoff = r - cascade_file ;
    output_file[cutoff-1] = '\0';
    printf("Downloading to: %s\n", output_file);
       
    casc_file = csc_parse_file(cascade_file, output_file);
    
    int uncomp_count = 0;
    queue = malloc(casc_file->blockcount * sizeof(csc_block_t*));

    /*
    TODO Create a list of missing blocks

    HINT: Use the already allocated 'csc_block_t** queue' for the list, and keep count of missing blocks in 'uncomp_cont'.
    */
    
    /*
    TODO Compute the hash of the cascade file

    HINT: Do not implement hashing from scratch. Use the provided 'get_file_sha' function
    */

    int peercount = 0;
    while (peercount == 0)
    {
        peercount = get_peers_list(&peers, hash_buf);
        if (peercount == 0)
        {
            printf("No peers were found. Will try again in %d seconds\n", PEER_REQUEST_DELAY);
            fflush(stdout);
            sleep(PEER_REQUEST_DELAY);
        }
        else
        {
            printf("Found %d peer(s)\n", peercount);
        }
    }
    
    csc_peer_t peer = (peers[0]);
    // Get a good peer if one is available
    for (int i=0; i<peercount; i++)
    {
        if (peers[i].good)
        {
            peer = peers[i];
        }
    }

    for (int i=0; i<uncomp_count; i++)
    {
        get_block(queue[i], peer, hash_buf, output_file);
    }
    
    free_resources();
}

/*
 * Count how many times a character occurs in a string
 */
int count_occurences(char string[], char c)
{
    int i=0;
    int count=0;
    for(i=0; i<strlen(string); i++)  
    {
        if(string[i] == c)
        {
            count++;
        }
    }
    return count;
}

// Adapted from: https://stackoverflow.com/a/35452093/
/*
 *  Convert a string of hexidecimal into a string of bytes
 */
uint8_t* hex_to_bytes(const char* string) {

    if(string == NULL) 
       return NULL;

    size_t slength = strlen(string);
    if((slength % 2) != 0) // must be even
       return NULL;

    size_t dlength = slength / 2;

    uint8_t* data = malloc(dlength);
    memset(data, 0, dlength);

    size_t index = 0;
    while (index < slength) {
        char c = string[index];
        int value = 0;
        if(c >= '0' && c <= '9')
          value = (c - '0');
        else if (c >= 'A' && c <= 'F') 
          value = (10 + (c - 'A'));
        else if (c >= 'a' && c <= 'f')
          value = (10 + (c - 'a'));
        else {
          free(data);
          return NULL;
        }

        data[(index/2)] += value << (((index + 1) % 2) * 4);

        index++;
    }

    return data;
}


/*
 * Parses a cascade file, given the sourcepath input and destination, which may or may not exist.
 * Returns a pointer to a datastructure describing the file, or NULL if the file could not be parsed
 */
csc_file_t* csc_parse_file(const char* sourcefile, const char* destination)
{
    FILE* fp = fopen(sourcefile, "rb");
    if (fp == 0)
    {
        printf("Failed to open source: %s\n", sourcefile);
        return NULL;
    }

    const int FILE_HEADER_SIZE = 8+8+8+8+32;

    char header[FILE_HEADER_SIZE];
    if (fread(header, 1, FILE_HEADER_SIZE, fp) != FILE_HEADER_SIZE)
    {
        printf("Failed to read magic 8 bytes header from file\n");
        fclose(fp);
        return NULL;
    }

    if (memcmp(header, "CASCADE1", 8) != 0)
    {
        printf("File does not contain magic 8 bytes in header\n");
        fclose(fp);
        return NULL;
    }

    // Hvad går opgaven ud på i sin helhed. 
    // Hvordan kan man teste om tingene virker. 

    csc_file_t* casc_file_data = (csc_file_t*)malloc(sizeof(csc_file_t));

    // targetsize:
    casc_file_data->targetsize = be64toh(*((unsigned long long*)&header[16]));

    //blocksize:
    casc_file_data->blocksize = be64toh(*((unsigned long long*)&header[24]));

    double size = casc_file_data->targetsize;
    double blocksize = casc_file_data->blocksize;


    //blockcount:
    casc_file_data->blockcount = floor((size + blocksize - 1)/blocksize);

    // blocks - pointer to array of all blocks.
    csc_block_t* blockArry;
    for (int i = 0; i<casc_file_data->blockcount; i++) {
        blockArry[i].index = 0;
        blockArry[i].offset = i*casc_file_data->blocksize + 64;
        blockArry[i].length = casc_file_data->blocksize;
        blockArry[i].completed;     // den her fatter vi ikke
        blockArry[i].hash;          // skal findes ved brug af source filen?
    }


    // trailsize:
    casc_file_data->trailblocksize = casc_file_data->targetsize % casc_file_data->blocksize;

    // hash:
    for (int i = 0; i<32; i++ ) {
        casc_file_data->targethash.x[i] = header[i+32];
    }




    
    /*
    TODO Parse the cascade file and store the data in an appropriate data structure    
    
    HINT Use the definition of the 'csc_file' struct in cascade.h, as well as the 
    assignment handout for guidance on what each attribute is and where it is stored 
    in the files header/body.
    */
    
    fclose(fp);

    fp = fopen(destination, "a+w");
    if (fp == NULL)
    {
        printf("Failed to open destination file %s\n", destination);
        csc_free_file(casc_file_data);
        return NULL;
    }
    
    void* buffer = malloc(casc_file_data->blocksize);
    if (buffer == NULL)
    {
        printf("No block buffer asigned: %d\n", casc_file_data->blocksize);
        csc_free_file(casc_file_data);
        fclose(fp);
        return NULL;
    }
    
    SHA256_CTX shactx;
    for(unsigned long long i = 0; i < casc_file_data->blockcount; i++)
    {
        char shabuffer[SHA256_HASH_SIZE];
        unsigned long long size = casc_file_data->blocks[i].length;        
        if (fread(buffer, size, 1, fp) != 1)
        {
            break;
        }

        sha256_init(&shactx);
        sha256_update(&shactx, buffer, size);
        sha256_final(&shactx, &shabuffer);
        
        

        /*
        TODO Compare the hashes taken from the Cascade file with those of the local data 
        file and keep a record of any missing blocks
        
        HINT The code above takes a hash of each block of data in the local file in turn 
        and stores it in the 'shabuffer' variable. You can compare then compare 'shabuffer'
        directly to the hashes of each block you have hopefully already assigned as part 
        of the 'casc_file_data' struct
        */
    }
    fclose(fp);

    return casc_file_data;
}

/*
 * Releases the memory allocated by a file datastructure
 */
void csc_free_file(csc_file_t* file)
{
    free(file->blocks);
    file->blocks = NULL;
    free(file);
}

/*
 * Get a specified block from a peer on the network. The block is retrieved and then inserted directly into
 * the appropriate data file at the appropriate location.
 */
void get_block(csc_block_t* block, csc_peer_t peer, unsigned char* hash, char* output_file)
{
    printf("Attempting to get block %d from %s:%s for %s\n", block->index, peer.ip, peer.port, output_file);
    
    rio_t rio;    
    char rio_buf[MAXLINE];
    int peer_socket;
    
    /*
    TODO Request a block from a peer
    
    HINT: Remember that integers sent over a network in TCP are done so in network byte order (big-endian) and 
    will need to be converted to a more useful format.
    
    HINT: Remember to check that the data you have recieved is the data you expect. You can check this as you
    should already have a hash of block in block->hash, and you can get the hash of the data you just read by
    using the function 'get_data_sha'
    */
    
    FILE* fp = fopen(output_file, "rb+");
    if (fp == 0)
    {
        printf("Failed to open destination: %s\n", output_file);
        Close(peer_socket);
        return;
    }

    /*
    TODO Write the block into the data file
    
    HINT: You can write a block to a specific index using the fseek(...) function. Check the documentation 
    online for details on how this work and what it does
    */
    
    printf("Got block %d. Wrote from %d to %d\n", block->index, block->offset, block->offset+write_count-1);
    Close(peer_socket);
    fclose(fp);
}

/*
 * Get a list of peers on the network from a tracker. Note that this query is doing double duty according to
 * the protocol, and by asking for a list of peers we are also enrolling on the network ourselves.
 */
int get_peers_list(csc_peer_t** peers, unsigned char* hash)
{
    rio_t rio;    
    char rio_buf[MAXLINE];
    int tracker_socket;
    
    /*
    TODO Setup a connection to the tracker
    
    HINT: Remember that as well as making a connection, you'll also need to initialise a buffer for messages
    */

    struct RequestHeader request_header;
    strncpy(request_header.protocol, "CASC", 4);
    request_header.version = htonl(1);
    request_header.command = htonl(1);
    request_header.length = htonl(BODY_SIZE);
    memcpy(rio_buf, &request_header, HEADER_SIZE);

    /*
    TODO Complete the peer list request

    HINT: The header has been provided above as a guide

    HINT: Take another look the specification of the Tracker API in the
          assingment text and at 'struct RequestBody' in cascade.h

    HINT: The client ip, i.e., 'my_ip', is not in a very useful representation...
    */
    
    Rio_writen(tracker_socket, rio_buf, MESSAGE_SIZE);

    Rio_readnb(&rio, rio_buf, MAXLINE);
    
    char reply_header[REPLY_HEADER_SIZE];
    memcpy(reply_header, rio_buf, REPLY_HEADER_SIZE);  

    uint32_t msglen = ntohl(*(uint32_t*)&reply_header[1]);
    if (msglen == 0)
    {
        Close(tracker_socket);
        return 0;        
    }
    
    if (reply_header[0] != 0) 
    {
        char* error_buf = malloc(msglen + 1);
        if (error_buf == NULL)
        {
            printf("Tracker error %d and out-of-memory reading error\n", reply_header[0]);
            Close(tracker_socket);
            return NULL;
        }
        memset(error_buf, 0, msglen + 1);
        memcpy(error_buf, &rio_buf[REPLY_HEADER_SIZE], msglen); // Fixed by Rune
        printf("Tracker gave error: %d - %s\n", reply_header[0], error_buf);
        free(error_buf);
        Close(tracker_socket);
        return NULL;
    }

    if (msglen % 12 != 0)
    {
        printf("LIST response from tracker was length %llu but should be evenly divisible by 12\n", msglen);
        Close(tracker_socket);
        return NULL;
    }
    
    /*
    TODO Parse the body of the response to get a list of peers
    
    HINT Some of the later provided code expects the peers to be stored in the ''peers' variable, which 
    is an array of 'csc_peer's, as defined in cascade.h
    */

    Close(tracker_socket);
    return peercount;
}

/*
 * The entry point for the code. Parses command line arguments and starts up the appropriate peer code.
 */
int main(int argc, char **argv) 
{
    if (argc != MAIN_ARGNUM + 1) 
    {
        fprintf(stderr, "Usage: %s <cascade file(s)> <tracker server ip> <tracker server port> <peer ip> <peer port>.\n", argv[0]);
        exit(EXIT_FAILURE);
    } 
    else if (!is_valid_ip(argv[2])) 
    {
        fprintf(stderr, ">> Invalid tracker IP: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    } 
    else if (!is_valid_port(argv[3])) 
    {
        fprintf(stderr, ">> Invalid tracker port: %s\n", argv[3]);
        exit(EXIT_FAILURE);
    } 
    else if (!is_valid_ip(argv[4])) 
    {
        fprintf(stderr, ">> Invalid peer IP: %s\n", argv[4]);
        exit(EXIT_FAILURE);
    } 
    else if (!is_valid_port(argv[5])) 
    {
        fprintf(stderr, ">> Invalid peer port: %s\n", argv[5]);
        exit(EXIT_FAILURE);
    }

    snprintf(tracker_ip,   IP_LEN,   argv[2]);
    snprintf(tracker_port, PORT_LEN, argv[3]);
    snprintf(my_ip,   IP_LEN,   argv[4]);
    snprintf(my_port, PORT_LEN, argv[5]);
    
    char cas_str[strlen(argv[1])];
    snprintf(cas_str, strlen(argv[1])+1, argv[1]);
    char delim[] = ":";

    int casc_count = count_occurences(argv[1], ':') + 1;
    char *cascade_files[casc_count];

    char *ptr = strtok(cas_str, delim);
    int i = 0;

    while (ptr != NULL)
    {
        if (strstr(ptr, ".cascade") != NULL)
        {
            cascade_files[i++] = ptr;
            ptr = strtok(NULL, delim);
        }
        else
        {
            printf("Abort on %s\n", ptr);   
            fprintf(stderr, ">> Invalid cascade file: %s\n", ptr);
            exit(EXIT_FAILURE);
        }
    }

    for (int j=0; j<casc_count; j++)
    {
        download_only_peer(cascade_files[j]);
    }

    exit(EXIT_SUCCESS);
}
