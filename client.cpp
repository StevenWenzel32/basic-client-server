#include <iostream>
#include <sys/types.h>    // socket, bind
#include <sys/socket.h>   // socket, bind, listen, inet_ntoa
#include <netinet/in.h>   // htonl, htons, inet_ntoa
#include <arpa/inet.h>    // inet_ntoa
#include <netdb.h>        // gethostbyname
#include <unistd.h>       // read, write, close
#include <strings.h>      // bzero
#include <netinet/tcp.h>  // SO_REUSEADDR
#include <sys/uio.h>      // writev
#include <string.h>       // memset
#include <errno.h>        // errno
#include <sys/time.h>     // gettimeofday

using namespace std;

// does the time calcs and handles negative usec values
pair<long, long> calcTime(const struct timeval &start, const struct timeval &stop) {
    // do the calcs
    long sec = stop.tv_sec - start.tv_sec;
    long usec = stop.tv_usec - start.tv_usec;

    // take a sec to fix negative usec - pun intended
    if (usec < 0) {
        sec -= 1;             
        usec += 1000000;
    }
    return {sec, usec};
}

int main (int argc, char* argv[]) {
    // check that the command line the right # of params
    if (argc < 7){
        cerr << "not enough parameters passed in. Usage: " << argv[0] << " <port>, <repetition>, <nbufs>, <bufsize>, <serverIp>, <type>\n";
        return 1;
    }

    // params passed in through command line
    // port number the client will use to connect to server
    const char* PORT = argv[1];
    // the repetition of sending a set of data buffers 
    int repetition = atoi(argv[2]);
    // number of data buffers
    int nbufs = atoi(argv[3]);
    // size of each data buffer in bytes
    int bufsize = atoi(argv[4]);
    // a server IP or name
    const char* serverIp  = argv[5];
    // the type of transfer scenario
    int type = atoi(argv[6]); 
      
    // for checking the return of getaddrinfo
    int status;
    // holds the info for the client address
    struct addrinfo client_addr;
    // points to the results that are in a linked list
    struct addrinfo *servinfo; 
    
    // create the struct and address info
    // make sure the struct is empty
    memset(&client_addr, 0, sizeof client_addr);
    // doesn't matter if its ipv4 or ipv6
    client_addr.ai_family = AF_UNSPEC;
    // tcp stream sockets
    client_addr.ai_socktype = SOCK_STREAM;
    
    // getaddrinfo with error check
    if ((status = getaddrinfo(serverIp, PORT, &client_addr, &servinfo)) != 0 ) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    
    // open a stream-oriented socket with the internet address family
    int clientSd = socket(servinfo->ai_family, servinfo->ai_socktype, 0);
    // check for error
    if(clientSd == -1){
        cerr << "failed to make socket" << endl;
    }

    // connect the socket to the server
    int connectStatus = connect(clientSd, servinfo->ai_addr, servinfo->ai_addrlen);
    // check for error
    if(connectStatus == -1){
        cerr << "failed to connect to the server" << endl;
    }

    // free the linked list -- all done with our connections 
    freeaddrinfo(servinfo);

    // list of buffers where nbufs * bufsize = 1500
    char databuf[nbufs][bufsize];
    // initialize buffers
    memset(databuf, 1, sizeof(databuf));

    // start of sending data
    struct timeval start;
    // time of done sending data
    struct timeval lap;
    // time when received acknowledgment from server
    struct timeval stop;

    // start timer and check for error
    if(gettimeofday(&start, NULL) == -1){
        cerr << "error starting timer" << endl;
    }

    // repeatedly send the writes until done
    for (int i = 0; i < repetition; i++){
        // type 1 - multiple writes
        if (type == 1){
            for (int j = 0; j < nbufs; j++){
                int bytes_sent = write(clientSd, databuf[j], bufsize);
                if (bytes_sent == -1){
                    cerr << "Problem with write" << endl;
                }
            }
        // type 2 - writev - makes an array of iovec structs that stores pointers to other data buffer and the buffer size
        } else if(type == 2){
            struct iovec vector[nbufs];
            for (int j = 0; j < nbufs; j++){
                vector[j].iov_base = databuf[j];
                vector[j].iov_len = bufsize;
            }
            int bytes_sent = writev(clientSd, vector, nbufs);
            // error check for write
            if (bytes_sent == -1){
                cerr << "Problem with write" << endl;
            }
        // type 3 - single write
        } else if(type == 3){
            // write the string and keep track of bytes sent
            int bytes_sent = write(clientSd, databuf, nbufs * bufsize);
            if (bytes_sent == -1){
                cerr << "Problem with write" << endl;
            }
        // bad type 
        } else {
            cerr << "Invalid type: " << type << endl;
        }
    }

    // lap the timer to help get the data-sending time
    if(gettimeofday(&lap, NULL) == -1){
        cerr << "error laping timer" << endl;
    }

    // receive acknowledgement from the server with # of read()
    int count = 0;
    int bytes_recv = 0;
    while (bytes_recv < sizeof(count)){
        int nRead = read(clientSd, (char*) &count, sizeof(count));
        if (nRead == -1){
            cerr << "Error reading from socket: clientSd - " << clientSd << endl;  
        }
        bytes_recv += nRead;
    }
    
    // stop the timer to help get the round-trip time
    if(gettimeofday(&stop, NULL) == -1){
        cerr << "error stopping timer" << endl;
    }
     
    // get the send and rt times
    pair<long, long> sendTime = calcTime(start, lap);
    pair<long, long> rtTime = calcTime(start, stop);
    
    // print out the statistics
    cout << "Test type " << type << ": data-sending time = " << sendTime.first << " sec. " << sendTime.second << " usec, round-trip time = " << rtTime.first << " sec. " << rtTime.second << " usec, #reads across all repetitions = " << count << ", #reads within each repetition is about = " << (double)(count/20000) << endl;
    // print out dashed line
    cout << "-----------" << endl;
    
    // close the client socket
    int bye = close(clientSd);
    if (bye == -1){
        cerr << "Error closing socket" << endl;
    }

    return 0;
}
