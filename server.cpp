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
#include <errno.h>        // errno
#include <string.h>       // for memset
#include <sys/time.h>     // gettimeofday

using namespace std;

const unsigned int BUFSIZE = 1500;

// struct to pass data to threads
struct thread_data {
    int sd;
    int repetition;
};

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

// function that the threads call. reads the # of repetition times
void* processRequest(void *ptr) {
    // cast back into data struct
    struct thread_data *data = (thread_data*) ptr;
    // buffer for reading the data
    char databuf[BUFSIZE];
    // start of receiving data
    struct timeval start;
    // time when finished receiving time
    struct timeval stop;
    
    // start timer
    if (gettimeofday(&start, NULL) == -1) {
        cerr << "error stopping timer" << endl;
    }
    
    // # of read calls
    int count = 0;
    for (int i = 0; i < data->repetition; i++) {
        int totalRead = 0;
        while (totalRead < BUFSIZE) {
            int nRead = read(data->sd, databuf, BUFSIZE - totalRead);
            if (nRead <= 0) {
                // Handle error or connection close
                if (nRead == -1) {
                    cerr << "Error reading from socket: sd - " << data->sd << endl;
                } else if (nRead == 0) {
                    cerr << "Client closed the connection" << endl;
                }
                break;
            }
            totalRead += nRead;
            count++;  
        }
    }
    
    // stop timer
    if (gettimeofday(&stop, NULL) == -1) {
        cerr << "error stopping timer" << endl;
    }
    
    // send # of read calls to client as an acknowledgement
    int bytes_sent = 0;
    while (bytes_sent < sizeof(count)) {
        int sent = write(data->sd, (char*) &count, sizeof(count));
        if (sent == -1) {
            cerr << "Problem with write to client" << endl;
        }
        bytes_sent += sent;
    }
       
    // get the recv time
    pair<long, long> recvTime = calcTime(start, stop);
     
    // print out the stats
    cout << "Data-receiving time = " << recvTime.first << " sec." << recvTime.second << " usec." << endl;  
    
    // Close the socket connection
    int bye = close(data->sd);
    if (bye == -1) {
        cerr << "Error closing socket" << endl;
    }
    
    // free thread data
    delete data;
    
    return nullptr;
}

int main (int argc, char* argv[]) {
    // the port users will connect to 
    const char* PORT = argv[1]; 
    // the repetition of sending a set of data buffers
    int repetition = atoi(argv[2]);
    
    // # of connection requests for the server to listen to at a time, used in listen call
    int BACKLOG = 10;
    // connector's address information can be either IPv4 or IPv6
    struct sockaddr_storage their_addr;
    // holds the info for the server address
    struct addrinfo server_addr;
    // points to the linked list of addrinfos
    struct addrinfo *servinfo; 
    // for checking the return of getaddrinfo
    int status;
    
    // create the struct and address info
    // make sure the struct is empty
    memset(&server_addr, 0, sizeof(server_addr));
    // doesn't matter if its ipv4 or ipv6
    server_addr.ai_family = AF_UNSPEC;
    // tcp stream sockets
    server_addr.ai_socktype = SOCK_STREAM;
    // fill in my IP for me 
    server_addr.ai_flags = AI_PASSIVE;
    
    // getaddrinfo and error check in one
    if ((status = getaddrinfo(NULL, PORT, &server_addr, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    
    // Create the listening socket on serverSd
    // goes into the linked list of addrinfos and uses them to fill in the socket params
    int serverSd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    // check if the socket call had an error
    if (serverSd == -1) {
        cerr << "error making the socket: serverSd - " << serverSd << endl;
    }
    
    // Enable socket reuse without waiting for the OS to recycle it
    // set the so-reuseaddr option
    const int on = 1;
    setsockopt(serverSd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(int));
    
    // Bind the socket to the port we passed into getaddrinfo which should be the local address in this case
    int binding = bind(serverSd, servinfo->ai_addr, servinfo->ai_addrlen);
    // check if the bind had an error
    if (binding == -1) {
        cerr << "Error binding socket: serverSd - " << serverSd << " to port: " << PORT << endl;
    }
    
    // instruct the OS to Listen to up to n connection requests on the socket
    int listening = listen(serverSd, BACKLOG);
    // check if listen has an error
    if (listening == -1) {
        cerr << "Error listening on socket: serverSd - " << serverSd << endl;
    } else {
        printf("Server: Waiting for connections...\n");
    }
    
    // free the linked list of addrinfos
    freeaddrinfo(servinfo);
    
    // size of clients address
    socklen_t their_AddrSize = sizeof(their_addr);
    
    while (1) {
        // Accept the connection as a new socket
        int newSd = accept(serverSd, (struct sockaddr *)&their_addr, &their_AddrSize);
        
        // check if the connection was made properly
        if (newSd == -1) {
            // error message
            cerr << "Error accepting connection on socket: serverSd - " << serverSd << endl;
        }
        
        // create a new thread
        pthread_t new_thread;
        // create thread data
        struct thread_data *data = new thread_data;
        data->repetition = repetition;
        data->sd = newSd;
        
        // start the thread
        int iretl = pthread_create(&new_thread, NULL, processRequest, (void*) data);
        // check for thread creation error
        if (iretl != 0) {
            cerr << "Error making thread" << endl; 
            delete data;
        }
        
        // wait for the thread to end
        pthread_join(new_thread, NULL);
    }
    
    // no need to listen after infinite loop
    int bye = close(serverSd);
    if (bye == -1) {
        cerr << "Error closing socket" << endl;
    }
    
    return 0;
}
