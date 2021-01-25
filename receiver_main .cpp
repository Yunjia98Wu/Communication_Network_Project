/* 
 * File:   receiver_main.c
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

#include <iostream>
#include <string>
#include <vector>
#include <queue>
using namespace std;

#define MAX_PAYLOAD 1472
#define RTT 20 * 1000 // 20ms = 20 * 1000 us
#define RECEIVER_BUFFER_SIZE 1000000
#define MAX_SEQ_NUM 1000000
#define MAX_FILE_BUFFER 2000

#define SYN 0
#define ACK 1
#define DATA 2
#define FIN 3
#define FINACK 4
#define SYNACK 5

struct TCP_PKT {
    int seq_num;
    int ack_num;
    int data_size;
    int type;
    int cw;
    char data[MAX_PAYLOAD];
};

/* receiver buffer */
vector<TCP_PKT> receiver_buffer(RECEIVER_BUFFER_SIZE);
vector<int> received(RECEIVER_BUFFER_SIZE, 0);
bool exists_out_of_order_pkt = false;

struct sockaddr_in si_me, si_other;
int s;
socklen_t slen;
void diep(string s) {
    perror(s.c_str());
    exit(1);
}

/* variables */
int desired_seq_num = 1;
int front = 0;

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    int numbytes;
    FILE* fp = fopen(destinationFile, "wb");

    /* regular UDP setup */
    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        diep("socket");
    }

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1) {
        diep("bind");
    }

    /* 3 way handshake setup */
    while (1) {
        char synack_buf[sizeof(TCP_PKT)];
        if ((numbytes = recvfrom(s, synack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*)&si_other, &slen)) == -1) {
            perror("receiver : receive error");
            exit(1);
        }
        TCP_PKT synack_pkt;
        memcpy(&synack_pkt, synack_buf, sizeof(TCP_PKT));
        if (synack_pkt.type == SYN) {
            cout << "receiver: received SYN pkt" << endl;
            synack_pkt.type = SYNACK;
            memcpy(synack_buf, &synack_pkt, sizeof(TCP_PKT));
            if ((numbytes = sendto(s, synack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, slen)) == -1) {
                perror("receiver : send error");
                exit(1);
            }
            cout << "receiver: sent SYNACK pkt" << endl;
            cout << "receiver : received ACK pkt, 3 way handshake SYN finished" << endl;
            break;

           /*if ((numbytes = recvfrom(s, synack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*)&si_other, &slen)) == -1) {
                perror("receiver : receive error");
                exit(1);
            }
            memcpy(&synack_pkt, synack_buf, sizeof(TCP_PKT));
            if (synack_pkt.type == ACK) {
                cout << "receiver : received ACK pkt, 3 way handshake SYN finished" << endl;
                break;
            }*/
        }
    }

	/* Now receive data and send acknowledgements */
    while (1) {
        char data_buf[sizeof(TCP_PKT)];
        if ((numbytes = recvfrom(s, data_buf, sizeof(TCP_PKT), 0, (struct sockaddr*)&si_other, &slen)) == -1) {
            perror("receiver : receive error");
            exit(1);
        }
        TCP_PKT data_pkt;
        memcpy(&data_pkt, data_buf, sizeof(TCP_PKT));
        if (data_pkt.type == DATA) {
            cout << "receiver : received data pkt NO." << data_pkt.seq_num << endl;
            // received in-order packet, directly write into file, do not need to store in buffer
            if (data_pkt.seq_num == desired_seq_num) {
                char file_buf[MAX_FILE_BUFFER];
                memcpy(file_buf, data_pkt.data, sizeof(data_pkt.data));
                fwrite(file_buf, sizeof(char), sizeof(data_pkt.data), fp);
                ++desired_seq_num;
                // find the last received pkt's seq_num's index
                int tmp_desired_index = desired_seq_num - 1;
                int last_ack_index = tmp_desired_index;
                for (int i = last_ack_index; i < RECEIVER_BUFFER_SIZE; ++i) {
                    if (received[i] == 0) {
                        last_ack_index = i - 1;
                        break;
                    }
                }
                // write the following in-order pkts into the file
                for (int i = tmp_desired_index; i <= last_ack_index; ++i) {
                    memcpy(file_buf, receiver_buffer[i].data, sizeof(receiver_buffer[i].data));
                    fwrite(file_buf, sizeof(char), sizeof(receiver_buffer[i].data), fp);
                    ++desired_seq_num;
                }
            }
            // received out-of-order packet, store in buffer
            else if (data_pkt.seq_num > desired_seq_num){
                int cur_pkt_index = data_pkt.seq_num - 1;
                memcpy(&receiver_buffer[cur_pkt_index], &data_pkt, sizeof(TCP_PKT));
                received[cur_pkt_index] = 1;
            }
            // send ACK pkt
            TCP_PKT ack_pkt;
            ack_pkt.ack_num = desired_seq_num-1;
            ack_pkt.type = ACK;
            char ack_buf[sizeof(TCP_PKT)];
            memcpy(ack_buf, &ack_pkt, sizeof(TCP_PKT));
            if ((numbytes = sendto(s, ack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, slen)) == -1) {
                perror("receiver : receive error");
                exit(1);
            }
            cout << "sender : sent ACK NO." << desired_seq_num-1 << endl;
        }
        else if (data_pkt.type == FIN) {
            cout << "receiver: received FIN pkt" << endl;
            TCP_PKT finack_pkt;
            finack_pkt.type = FINACK;
            char finack_buf[sizeof(TCP_PKT)];
            memcpy(finack_buf, &finack_pkt, sizeof(TCP_PKT));
            if ((numbytes = sendto(s, finack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, slen)) == -1) {
                perror("receiver : send error");
                exit(1);
            }
            cout << "receiver : sent FINACK pkt" << endl;
            cout << "receiver : received ACK pkt, 3 way handshake FIN finished" << endl;
            break;
            /*if ((numbytes = recvfrom(s, finack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, &slen)) == -1) {
                perror("receiver : receive error");
                exit(1);
            }
            memcpy(&finack_pkt, finack_buf, sizeof(TCP_PKT));
            if (finack_pkt.type == ACK) {
                cout << "receiver : received ACK pkt, 3 way handshake FIN finished" << endl;
                break;
            }*/
        }
    }

    /* 3 way handshake FIN */
    /*
    while (1) {
        char finack_buf[sizeof(TCP_PKT)];
        if ((numbytes = recvfrom(s, finack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*)&si_other, &slen)) == -1) {
            perror("receiver : receive error");
            exit(1);
        }
        TCP_PKT finack_pkt;
        memcpy(&finack_pkt, finack_buf, sizeof(TCP_PKT));
        if (finack_pkt.type == FIN) {
            cout << "receiver: received FIN pkt" << endl;
            finack_pkt.type = FINACK;
            memcpy(finack_buf, &finack_pkt, sizeof(TCP_PKT));
            if ((numbytes = sendto(s, finack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, slen)) == -1) {
                perror("receiver : send error");
                exit(1);
            }
            cout << "receiver : sent FINACK pkt" << endl;
            if ((numbytes = recvfrom(s, finack_buf, sizeof(TCP_PKT), 0, (struct sockaddr*) &si_other, &slen)) == -1) {
                perror("receiver : receive error");
                exit(1);
            }
            memcpy(&finack_pkt, finack_buf, sizeof(TCP_PKT));
            if (finack_pkt.type == ACK) {
                cout << "receiver : received ACK pkt, 3 way handshake FIN finished" << endl;
                break;
            }
        }
    }
    */

    close(s);
	printf("%s received.", destinationFile);
    return;
}

int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}
