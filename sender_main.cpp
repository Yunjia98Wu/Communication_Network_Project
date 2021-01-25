/* 
 * File:   sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include <iostream>
#include <string>
#include <vector>
#include <queue>
using namespace std;

// TODO: 3-way hand shake, congestion control, flow control, queue, buffer, timeout
#define SLOW_START 0
#define CONGESTION_AVOIDANCE 1
#define FAST_RECOVERY 2

#define MAX_PAYLOAD 1400
#define RTT 20 * 1000 // 20ms = 20 * 1000 us
#define BUFFER_SIZE 1000
#define MAX_SEQ_NUM 1000000

#define SYN 0
#define ACK 1
#define DATA 2
#define FIN 3
#define FINACK 4
#define SYNACK 5

/* Global variables */
struct sockaddr_in si_other;
int s, slen;
struct sockaddr_storage con_addr;
socklen_t con_addrlen=sizeof con_addr;
// Congestion control
int global_seq_num = 1;
int last_sent_index = 0; // equals seq_num - 1
int last_ack_num=0;
int cw = 1;
int cw_front = 0;
int cw_rear = cw_front + cw - 1;
int ss_threshold = 64;
int dupACK_nums = 1;
int congestion_state = SLOW_START;
int cong_avoid_counter = 0; // deal with cw += 1/cw case
bool is_newACK = false;
bool is_dupACK = false;

// Timeout variable
//__darwin_suseconds_t global_time;
bool is_timeout = false;

/* TCP Data Formula */
struct TCP_PKT {
    int seq_num;
    int ack_num;
    int data_size;
    int type;
    int cw;
    char data[MAX_PAYLOAD];
};

// queue for sender and buffer for receiver

vector<TCP_PKT> sender_buffer;
// vector<TCP_PKT> buffer(BUFFER_SIZE);

void diep(string s) {
    perror(s.c_str());
    exit(1);
}

/*bool IsTimeout() {
    struct timeval tv;// contains tv_sec, tv_usec (second, u second)
    gettimeofday(&tv, NULL);
    if (abs(tv.tv_usec - global_time) > 2 * RTT) {
        return true;
    }
    else {
        return false;
    }
}*/

void CongestionControl() {
    // deal with time out
    if (is_timeout) {
      cong_avoid_counter = 0;
        cout << "timeout"<< endl;
        ss_threshold = max(cw / 2,1);
        cw = 1;
        dupACK_nums = 0;
        congestion_state = SLOW_START;
        
    }
    else {
        if (congestion_state == SLOW_START) {
           cong_avoid_counter = 0;
            // new ack
            if (is_newACK) {
                ++cw;
                if (cw >= ss_threshold) {
                    congestion_state = CONGESTION_AVOIDANCE;
                }
                dupACK_nums = 0;
            }
            else if (is_dupACK) {
                ++dupACK_nums;
                if (dupACK_nums == 3) {
                    ss_threshold = max(cw / 2,1);
                    cw = ss_threshold + 3;
                    congestion_state = FAST_RECOVERY;
                }
            }
           cout << "slowstart"<< endl;
            cout << "CW"<< cw << endl;
           cout << "SSTH"<< ss_threshold << endl;
        }
        else if (congestion_state == CONGESTION_AVOIDANCE) {
           
            if (is_newACK) {
                ++cong_avoid_counter;
               cout <<"counter"<< cong_avoid_counter<< endl;
                if (cong_avoid_counter == cw) {
                    ++cw;
                    cong_avoid_counter = 0;
                }
                dupACK_nums = 0;
            }
            else if (is_dupACK) {
                ++dupACK_nums;
              
                if (dupACK_nums == 3) {
                    
                    ss_threshold = max(cw / 2,1);
                    cw = ss_threshold + 3;
                    congestion_state = FAST_RECOVERY;
                }
            }
         cout << "cong avoid"<< endl;
           cout << "CW"<< cw << endl;
           cout << "SSTH"<< ss_threshold << endl;
        }
        else if (congestion_state == FAST_RECOVERY) {
            cong_avoid_counter = 0;
     
            if (is_newACK) {
                cw = ss_threshold;
                dupACK_nums = 0;
                congestion_state = CONGESTION_AVOIDANCE;
            }
            else if (is_dupACK) {
                ++cw;
            }
           cout << "fast recovery"<< endl; 
           cout << "CW"<< cw << endl;
           cout << "SSTH"<< ss_threshold << endl;
        }
    }
}

void InitializeBuffer(FILE* fp, unsigned long long int total_bytes) {
    char read_buf[MAX_PAYLOAD];
    
    while (total_bytes > 0) {
         unsigned long long int bytes_to_read = total_bytes > MAX_PAYLOAD ? MAX_PAYLOAD : total_bytes;
        int read_result;
        if ((read_result = fread(read_buf, sizeof(char), bytes_to_read, fp)) == 0) {
            perror("fread error : not enough data");
            exit(1);
        }
        TCP_PKT pkt;
        pkt.data_size = bytes_to_read;
        pkt.type = DATA;
        pkt.cw = cw;
        pkt.seq_num = global_seq_num > MAX_SEQ_NUM ? global_seq_num : global_seq_num % MAX_SEQ_NUM;
        memcpy(pkt.data, read_buf, bytes_to_read);
        sender_buffer.push_back(pkt);
        total_bytes -= bytes_to_read;
        ++global_seq_num;
    }
cout << "we have pack num"<< global_seq_num << endl;
cout << "we have size"<< sender_buffer.size() << endl;
}

void SendPacket(int sockfd, int& numbytes, struct addrinfo* p) {
    cout << "cwfront" << cw_front<< endl;
    // Determine cw_rear
    if (cw_front + cw - 1 >= sender_buffer.size()) {
        cw_rear = sender_buffer.size() - 1;
    }
    else {
        cw_rear = cw_front + cw - 1;
    }
    if (last_ack_num==sender_buffer.size()) {
        cout << "no data to send" << endl;
        return;
    }
    if(last_sent_index==0&&cw_rear==0)
    { char firsttcp_to_char[sizeof(TCP_PKT)];
        memcpy(firsttcp_to_char, &sender_buffer[0], sizeof(TCP_PKT));
        if ((numbytes = sendto(sockfd, firsttcp_to_char, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("send : send pkt failure");
            exit(1);
        }
        cout << "sent pkt NO." << sender_buffer[0].seq_num << " successful." << endl;
    
    }
 else{
    for (int i = last_sent_index + 1; i <= cw_rear; ++i) {
        char tcp_to_char[sizeof(TCP_PKT)];
        memcpy(tcp_to_char, &sender_buffer[i], sizeof(TCP_PKT));
        if ((numbytes = sendto(sockfd, tcp_to_char, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("send : send pkt failure");
            exit(1);
        }
        cout << "sent pkt NO." << sender_buffer[i].seq_num << " successful." << endl;
  
    }}
    last_sent_index = max(cw_rear,last_sent_index);
     cout << "last_sent_index" << cw_rear<< endl;
   
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }
    /* Initialize buffer */
    InitializeBuffer(fp, bytesToTransfer);
    /* Regular UDP setup */
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(hostname, to_string(hostUDPport).c_str(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo : %s\n", gai_strerror(rv));
        return;
    }
    // loop through all the results and make a socket
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("sender : socket");
            continue;
        }
        break;
    }

    if(p == NULL) {
        fprintf(stderr, "sender: fail to make a socket");
        return;
    }

    /* Set timeout */
    struct timeval time_out;
    time_out.tv_sec = 0;
    time_out.tv_usec = 2 * RTT;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &time_out, sizeof(time_out)) == -1) {
        perror("sender : set time out fail");
    }

	/* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    /* 3 way handshake to initialize connection */
    while(1) {
        TCP_PKT syn_pkt;
        char syn_buf[sizeof(TCP_PKT)];
        syn_pkt.type = SYN;
        memcpy(syn_buf, &syn_pkt, sizeof(TCP_PKT));
        // first handshake
        if ((numbytes = sendto(sockfd, syn_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("reliablySender : send error");
            exit(1);
        }
        cout << "sender : sent SYN pkt" << endl;
        TCP_PKT syn_ack_pkt;
        // second handshake
        if ((numbytes = recvfrom(sockfd, syn_buf, sizeof(TCP_PKT), 0,(struct sockaddr*)&con_addr,&con_addrlen)) == -1) {
            perror("reliablySender : receive error");
            continue;
        }
        memcpy(&syn_ack_pkt, syn_buf, sizeof(TCP_PKT));
        if (syn_ack_pkt.type == SYNACK) {
            cout << "sender : receiverd SYNACK pkt" << endl;
            /*syn_pkt.type = ACK;
            memcpy(syn_buf, &syn_pkt, sizeof(TCP_PKT));
            // third handshake
            if ((numbytes = sendto(sockfd, syn_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
                perror("reliablySender : send error");
                exit(1);
            }*/
            cout << "sender : sent ACK pkt, 3 way handshake finished" << endl;
            break;
        }
    }

	/* Send data and receive acknowledgements on s*/
    SendPacket(sockfd, numbytes, p);
    while (last_ack_num<sender_buffer.size()) {
        char recv_buf[sizeof(TCP_PKT)];
        // Deal with timeout
        if ((numbytes = recvfrom(sockfd, recv_buf, sizeof(TCP_PKT), 0,(struct sockaddr*)&con_addr,&con_addrlen)) == -1) {
            CongestionControl();
            is_timeout = true;
            cout << "sender: Receive timeout, resend pkt NO." << last_ack_num + 1 << endl;
            char send_buf[sizeof(TCP_PKT)];
            memcpy(send_buf, &sender_buffer[last_ack_num], sizeof(TCP_PKT));
            if ((numbytes = sendto(sockfd, send_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
                perror("reliablySender : send error");
            }
            cout << "sender: Resend timeout pkt NO." << last_ack_num + 1 << " success" << endl;
 
        }
        else {
            is_timeout = false;
            TCP_PKT temp_pkt;
            memcpy(&temp_pkt, recv_buf, sizeof(TCP_PKT));
            if (temp_pkt.type == ACK) {
                cout << "sender: Received ACK NO." << temp_pkt.ack_num << endl;
                if (temp_pkt.ack_num == last_ack_num) {
                    
                    cout << "sender: Received dupACK NO." << temp_pkt.ack_num << endl;
                    is_newACK = false;
                    is_dupACK =  true;
                    if (dupACK_nums >= 2&&congestion_state!=2) {
                    
                        cout << "sender: Received 3 dupACK" << endl;
                        char dupACK_buf[sizeof(TCP_PKT)];
                        memcpy(dupACK_buf, &sender_buffer[last_ack_num], sizeof(TCP_PKT));
                        if ((numbytes = sendto(sockfd, dupACK_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
                            perror("reliablySender : send error");
                        }
                        cout << "sender: Resend dupACK pkt NO." << last_ack_num + 1 << " success" << endl;
                   
                 }
                 CongestionControl(); 
                 }
                     
               else {
                    is_newACK = true;
                    is_dupACK =  false;
                    last_ack_num = temp_pkt.ack_num;
                    cw_front=last_ack_num;
                    CongestionControl();
                }
            }
            else {
                perror("sender : Received pkt's type is not ACK");
                exit(1);
            }
        }
        SendPacket(sockfd,numbytes,p);
    }

    fclose(fp);

    /* 3 way handshake FIN */
    while(1) {
        TCP_PKT fin_pkt;
        fin_pkt.type = FIN;
        char fin_buf[sizeof(TCP_PKT)];
        memcpy(fin_buf, &fin_pkt, sizeof(TCP_PKT));
        if ((numbytes = sendto(sockfd, fin_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("reliablySender : send error");
            exit(1);
        }
        cout << "sent FIN pkt" << endl;
        TCP_PKT fin_ack_pkt;
        
        if ((numbytes = recvfrom(sockfd, fin_buf, sizeof(TCP_PKT), 0,(struct sockaddr*)&con_addr,&con_addrlen)) == -1) {
            perror("reliablySender : receive error");
            continue;
        }
        cout << "received FIN_ACK pkt" << endl;
        memcpy(&fin_ack_pkt, fin_buf, sizeof(TCP_PKT));
        if (fin_ack_pkt.type == FINACK) {
            /*fin_pkt.type = ACK;
            memcpy(fin_buf, &fin_pkt, sizeof(TCP_PKT));
            if ((numbytes = sendto(sockfd, fin_buf, sizeof(TCP_PKT), 0, p->ai_addr, p->ai_addrlen)) == -1) {
                perror("reliablySender : send error");
                continue;
            }*/
            cout << "sent ACK pkt, 3 way handshake FIN finished" << endl;
            break;
        }
    }

    printf("Closing the socket\n");
    close(s);
    return;

}

int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}

