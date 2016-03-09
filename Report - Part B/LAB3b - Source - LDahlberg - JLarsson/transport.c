#include "transport.h"


  /* =================
     USER CONTROLS
     ================= */

     /* Server & Client */
     /* --------------- */

     /* Start Protocol */
     void u_start(){
        printf("u_start()\n");


         window = 3;
         is_server = 0;
         neg_window_size = 0;
         server_socket_length = sizeof(server_socket_address);
         //server_ip = "127.0.0.111";

         /* Intializes random number generator */
         time_t t;
         srand((unsigned) time(&t));

         /* No acks sent yet */
         first_ack = 1;
         re_ack = 0;
         drop_rate = 1000000000;

         /* Buffer setup*/
         server_buf.seq_0 = 0; //Not used
         server_buf.seq_1 = 0; //Next packet to ack
         server_buf.seq_2 = 0; //Next packet to place on buffer

         client_buf.seq_0 = 0; //Next place to load new packet on buffer
         client_buf.seq_1 = 0; //Next packet to send
         client_buf.seq_2 = 0; //Next packet expecting ACK for

         ack_buf.seq_0 = 0; //Not used
         ack_buf.seq_1 = 0; //Next ack to process
         ack_buf.seq_2 = 0; //Next ack to place on buffer

         /* Reset timers for packages */
         int i = 0;
         while (i < MODULO) {
           reset_timer(&client_established[i]);
           i++;
         }

         /* Init state machine */
         state = CLOSED;
         input = NONE;

         /* Timers */
         reset_timer(&syn_sent_timer);
         reset_timer(&pre_established_timer);
         reset_timer(&syn_recieved_timer);
         reset_timer(&fin_wait_1_timer);
         reset_timer(&time_wait_timer);
         reset_timer(&closing_timer);
         reset_timer(&close_wait_timer);
         reset_timer(&last_ack_timer);

         /* Start state machine */
         if(pthread_create(&tid, NULL, STATE_MACHINE, 0) != 0){
           perror("Could not start thread.\n");
           exit(EXIT_FAILURE);
         }
     }

     /* Close protocol */
     void u_close(){
       printf("u_close()\n");
       input = CLOSE;
     }

     void u_exit(){
       printf("u_exit()\n");
       input = EXIT;
     }

     /* Server */
     /* ------ */

     /* Listen to connections */
     void u_listen(char* server_ip_address){
         printf("u_listen()\n");
         input = LISTEN;
         is_server = 1;
         if ((server_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
         {
             //die("socket");
             perror("Socket.\n");
             exit(EXIT_FAILURE);
         }
         // zero out the structure
         memset((char *) &server_socket_self_address, 0, sizeof(server_socket_self_address));

         server_socket_self_address.sin_family = AF_INET;
         server_socket_self_address.sin_port = htons(SERVER_PORT);
         server_socket_self_address.sin_addr.s_addr = inet_addr(server_ip_address);

         //bind socket to port
         if( bind(server_socket , (struct sockaddr*)&server_socket_self_address, sizeof(server_socket_self_address) ) == -1)
         {
             //die("bind");
             perror("Bind.\n");
             exit(EXIT_FAILURE);
         }
     }

     /* Start recieving packages from network onto buffer */
     void u_start_recieving()
     {
       printf("u_start_recieving()\n");
       /* Start thread recieving replies from server */
       if(pthread_create(&tid2, NULL, recieve_packets, NULL) != 0){
         perror("Could not start thread.\n");
         exit(EXIT_FAILURE);
       }
     }

     /* Set function for processing recieved data */
     void u_set_rcvr(void (*rcvr)(char*)){
       process_data = rcvr;
     }

     /* Client */
     /* ------ */

     /* Connect to server */
     void u_connect(char* server_ip_address){
         printf("u_connect()\n");
         input = CONNECT;
         id = rand()%2000000000;
         //Connect client
         if ((server_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
             {
                 //die("socket");
                 perror("Socket.\n");
                 exit(EXIT_FAILURE);
             }
         memset((char *) &server_socket_address, 0, sizeof(server_socket_address));
         server_socket_address.sin_family = AF_INET;
         server_socket_address.sin_port = htons(SERVER_PORT);


         // zero out the structure
         memset((char *) &client_address, 0, sizeof(client_address));
         client_address.sin_family = AF_INET;
         client_address.sin_port = htons(SERVER_PORT+1);
         client_address.sin_addr.s_addr = htonl(INADDR_ANY);
         //bind socket to port
         if( bind(server_socket , (struct sockaddr*)&client_address, sizeof(client_address) ) == -1)
         {
             //die("bind");
             perror("Bind.\n");
             exit(EXIT_FAILURE);
         }

         if (inet_aton(server_ip_address , &server_socket_address.sin_addr) == 0)
             {

                 fprintf(stderr, "inet_aton() failed\n");
                 exit(1);
             }
     }

     /* Make client recieve responses from server */
     void u_prep_sending(){
       printf("u_prep_sending()\n");

       /* Start thread recieving replies from server */
       if(pthread_create(&tid2, NULL, recieve_acks, 0) != 0){
         perror("Could not start thread.\n");
         exit(EXIT_FAILURE);
       }
     }

     /* Send stream of data to server */
     void u_send(char* data, int length){
      // printf("u_send()\n");
      // printf("=========\nSENDING: %s;=========\n",data);
       float p = (float) length / PACKET_DATA_SIZE;
       int packets = (int) ceil(p);
       int i = 0;
       char p_data[PACKET_DATA_SIZE];

       /* Divide data into packages and send */
       while(i < packets){
         memset(&p_data, 0, sizeof(PACKET_DATA_SIZE));
         if(i == packets-1){
           memcpy(&p_data, data+(i*PACKET_DATA_SIZE), length-(PACKET_DATA_SIZE*(packets-1)));
         }else{
           memcpy(&p_data, data+(i*PACKET_DATA_SIZE), PACKET_DATA_SIZE);
         }
         send_packet(p_data);
         i++;
       }
     }


     /* INTERNAL */
     /* ======== */

     /* SEND PACKET */
     /* Place package onto buffer */
     void send_packet(char data[32]){
       if(is_server == 0){
         client_buf.packet[client_buf.seq_0].id = id;
       }
       strcpy(client_buf.packet[client_buf.seq_0].data, data);
       client_buf.packet[client_buf.seq_0].seq = client_buf.seq_0;
       client_buf.packet[client_buf.seq_0].sum = ip_checksum(&client_buf.packet[client_buf.seq_0], sizeof(PACKET));

       //printf("WINDOW BUFFER >> %.*s\n", PACKET_DATA_SIZE, client_buf.packet[client_buf.seq_0].data);

       client_buf.seq_0 = next_seq(client_buf.seq_0);
     }

     /* SEND MESSAGE */
     /* Messages like ACK, SYN, FIN with or without sequence numbers */
     void send_message(int seq, int ack, int syn, int fin){
       PACKET ack_packet;
       ack_packet.ack = ack;
       ack_packet.seq = seq;
       ack_packet.syn = syn;
       ack_packet.fin = fin;
       ack_packet.sum = 0;


       if(is_server == 0){
         ack_packet.id = id;
         ack_packet.window_size = window;
       }else if(is_server == 1 && neg_window_size == 1){
         ack_packet.window_size = window;
       }else{
         ack_packet.window_size = -1;
       }

       ack_packet.sum = ip_checksum(&ack_packet, sizeof(PACKET));
       //printf("SEND PCKT SUM: %d;\n", ip_checksum(&ack_packet, sizeof(PACKET)));

       unsigned int slen;
       /* SERVER scenario */
       if(is_server == 1){
         slen = sizeof(server_socket_client_address);
         if((sendto(server_socket, &ack_packet, sizeof(PACKET), 0, (struct sockaddr *) &server_socket_client_address, slen))==-1)
           {
               perror("Sendto.\n");
               exit(EXIT_FAILURE);
           }
       }
       /* CLIENT scenario */
       else if(is_server == 0)
       {
           slen = sizeof(server_socket_address);
           if((sendto(server_socket, &ack_packet, sizeof(PACKET), 0, (struct sockaddr *) &server_socket_address, slen))==-1)
           {
               perror("Sendto.\n");
               exit(EXIT_FAILURE);
           }
       }
     }

     /* RECIEVE PACKETS ONTO BUFFER */
     /* Process special flags and check for errors */
     void* recieve_packets(void *arg){
         PACKET pack;
         while(FOREVER)
         {


             /* Kill thread if machine stops */
             if(state == EXITING){
                return NULL;
             }

             fflush(stdout);
             unsigned int slen = sizeof(server_socket_client_address);

             //try to receive some data, this is a blocking call
             if ((recv_len = recvfrom(server_socket, &pack, sizeof(PACKET), 0, (struct sockaddr *) &server_socket_client_address, &slen)) == -1)
             {
                 perror("Recvfrom.\n");
                 exit(EXIT_FAILURE);
             }

             if(ip_checksum(&pack, sizeof(PACKET)) == 0){

               if(pack.window_size < MODULO/2){
                 window = pack.window_size;
               }

               /* Check if packages is special flag */
               if(pack.syn == 1 && pack.ack == 1){
                 if(rand()%drop_rate != 1){
                    input = SYN_ACK;
                  }else{

                   printf("ERROR SIMULATION: DROPPED SYN_ACK\n");
                 }


               }else if(pack.fin == 1 && pack.ack == 1){
                 if(rand()%drop_rate != 1){
                   input = FIN_ACK;
                 }else{

                   printf("ERROR SIMULATION: DROPPED FIN_ACK\n");

                 }

               }else if(pack.fin == 1){
                 if(rand()%10 != 1){
                    input = FIN;
                  }else{
                   printf("ERROR SIMULATION: DROPPED FIN\n");

                 }

               }else if(pack.ack == 1){
                 if(rand()%drop_rate != 1){
                   input = ACK;
                 }else{

                   printf("ERROR SIMULATION: DROPPED ACK\n");
                 }

               }else if(pack.syn == 1){
                 if(rand()%drop_rate != 1){
                   input = SYN;
                 }else{
                   printf("ERROR SIMULATION: DROPPED SYN\n");
                 }
               }else{

                 /* Process standard package */

                   //Error simulation
                   if(pack.seq == server_buf.seq_1 && rand()%drop_rate == 1){
                     pack.seq = pack.seq + 1;
                     printf("ERROR SIMULATION: PACKET [Wrong order, seq: %d -> seq:%d]\n", server_buf.seq_1, pack.seq);
                   }


                   if(pack.seq == server_buf.seq_1){
                     //printf("RECIEVE PACKET: placed in server buffer\n");
                     //printf("Data: %s\n" , pack.data);
                     strcpy(server_buf.packet[server_buf.seq_2].data, pack.data);
                     server_buf.packet[server_buf.seq_2].ack = pack.ack;
                     server_buf.packet[server_buf.seq_2].fin = pack.fin;
                     server_buf.packet[server_buf.seq_2].syn = pack.syn;
                     server_buf.packet[server_buf.seq_2].sum = pack.sum;
                     server_buf.packet[server_buf.seq_2].seq = pack.seq;
                     server_buf.seq_2 = next_seq(server_buf.seq_2);
                   }else{
                     printf("RCVD: PACKET [Wrong order, seq: %d]\n", pack.seq);
                     //printf("ERROR: Packet out of order [SEQ: %d] (pack.seq:%d != server_buf.seq_1:%d)\n", pack.seq, pack.seq, server_buf.seq_1);
                     re_ack = 1;
                   }

               }
             }else{
               printf("RCVD: PACKET [Invalid checksum]: %d\n", pack.sum);
             }
         }
         return NULL;
     }

     /* RECIEVE ACKS */
     void* recieve_acks(void* arg)
     {
         PACKET ack;
         while(FOREVER)
         {

             /* Kill thread if machine stops */
             if(state == EXITING){
               return NULL;
             }

             fflush(stdout);

             unsigned int slen = sizeof(server_socket_address);
             //try to receive some data, this is a blocking call
             if ((recv_len = recvfrom(server_socket, &ack, sizeof(PACKET), 0, (struct sockaddr *) &server_socket_address, &slen)) == -1)
             {
                 perror("Recvfrom.\n");
                 exit(EXIT_FAILURE);
             }

             if(ip_checksum(&ack, sizeof(PACKET)) == 0){
               if(ack.window_size > 0){
                 window = ack.window_size;
               }

               if(rand()%drop_rate != 1){
                 //printf("DID NOT DROP FLAG\n");
                 /* Handle special flags */
                 if(ack.syn == 1 && ack.ack == 1){
                   input = SYN_ACK;
                 }else if(ack.fin == 1 && ack.ack == 1){
                   input = FIN_ACK;
                 }else if(ack.fin == 1){
                   input = FIN;
                 }else if(ack.ack == 1 && ack.seq == -1){
                   input = ACK;
                 }else if(ack.syn == 1){
                   input = SYN;
                 }else{
                   /* Handle normal seq. acks */
                   //printf("RECV ACK: %d\n", ack.seq);
                   strcpy(ack_buf.packet[ack_buf.seq_2].data, ack.data);
                   ack_buf.packet[ack_buf.seq_2].ack = ack.ack;
                   ack_buf.packet[ack_buf.seq_2].fin = ack.fin;
                   ack_buf.packet[ack_buf.seq_2].syn = ack.syn;
                   ack_buf.packet[ack_buf.seq_2].sum = ack.sum;
                   ack_buf.packet[ack_buf.seq_2].seq = ack.seq;
                   ack_buf.seq_2 = next_seq(ack_buf.seq_2);
                 }
               }else{
                 printf("ERROR SIMULATION: FLAG PACKET DROPPED [seq: %d, ack: %d, syn: %d, fin: %d]\n",ack.seq, ack.ack, ack.syn, ack.fin);
               }
             }else{
               printf("ERROR: Invalid checksum\n");
             }




         }
         return NULL;
     }

     /* TOOLS */
     /* ===== */

     /* WINDOW SIZE */
     int client_window(){

       if(client_buf.seq_1 >= client_buf.seq_2){
         return client_buf.seq_1 - client_buf.seq_2;
       }else{
         return MODULO - (client_buf.seq_1 - client_buf.seq_2);
       }
     }

     /* MODULO NEXT SEQ. IN INCOMING BUFFER */
     int next_seq(int seq){
       if(seq+1 > MODULO - 1){
         return 0;
       }else{
         return seq+1;
       }
     }

     /* MODULO NEXT ACK. IN INCOMING SLIDING WINDOW */
     int next_ack(int ack){
       if(ack == -1){
         return 0;
       }
       if(ack+1 > WINDOW_MODULO - 1){
         return 0;
       }else{
         return ack+1;
       }
     }

     /* MODULO PREV. ACK. IN INCOMING SLIDING WINDOW */
     int prev_ack(int ack){
       if(ack == 0){
         if(first_ack == 1){
           return 0;
         }else{
           return WINDOW_MODULO - 1;
         }

       }else{
         return ack-1;
       }
     }

     /* TIMER FUNCTIONS */
     int timeout(TIMER t){
         if(clock()-t.start >= t.length){
             return 1;
         }else{
             return 0;
         }
     }

     /* RESET TIMER */
     void reset_timer(TIMER *t){
       t->on = -1;
     }

     /* DECREASE TIMER COUNT */
     void decrease_timer(TIMER *t){
       if(t->on > 0){
         t->on--;
       }else{
         t->on = 0;
       }
     }

     /* GET TIMER TIME */
     clock_t clock_time(int milli_seconds){
         //return (milli_seconds/1000)*CLOCKS_PER_SEC;
         return 1000;
     }

     /* CHECKSUM */
     uint16_t ip_checksum(void* vdata,size_t length) {
         // Cast the data pointer to one that can be indexed.
         char* data=(char*)vdata;
         // Initialise the accumulator.
         uint32_t acc=0xffff;
         size_t i;
         // Handle complete 16-bit blocks.
         for (i=0;i+1<length;i+=2) {
             uint16_t word;
             memcpy(&word,data+i,2);
             acc+=ntohs(word);
             if (acc>0xffff) {
                 acc-=0xffff;
             }
         }
         // Handle any partial block at the end of the data.
         if (length&1) {
             uint16_t word=0;
             memcpy(&word,data+length-1,1);
             acc+=ntohs(word);
             if (acc>0xffff) {
                 acc-=0xffff;
             }
         }
         // Return the checksum in network byte order.
         return htons(~acc);
     }

     /* STATE MACHINE - OUTPUT */
     /* ====================== */

     void OUT_send_ack(int seq){
       printf("SENT: ACK [seq: %d]\n", seq);
       send_message(seq, 1, 0, 0);
     }

     void OUT_send_syn(){
       printf("SENT: SYN\n");
       send_message(-1, 0, 1, 0);
     }

     void OUT_send_packet(PACKET p){
       //printf("OUT: send packet; DATA = %s;\n", p.data);
       printf("SENT: PACKET [SEQ:%d, DATA:%.*s]\n", p.seq, PACKET_DATA_SIZE, p.data);
       /* ERROR SIMULATION */
       p.sum = 0;
       p.sum = ip_checksum(&p, sizeof(PACKET));
       if(rand()%drop_rate == 1){
         p.sum = 0;
         printf("ERROR SIMULATION: PACKET [INVALID CHECKSUM: SEQ:%d]\n", p.seq);
       }

       if(sendto(server_socket, &p, sizeof(PACKET), 0, (struct sockaddr *) &server_socket_address, server_socket_length)==-1)
         {
             perror("Sendto.\n");
             exit(EXIT_FAILURE);
         }
     }

     void OUT_send_syn_ack(){
       printf("SENT: SYN_ACK\n");
       send_message(-1, 1, 1, 0);
     }

     void OUT_send_fin(){
       printf("SENT: FIN\n");
       send_message(-1, 0, 0, 1);
     }

     void OUT_send_fin_ack(){
       printf("SENT: FIN_ACK\n");
       send_message(-1, 1, 0, 1);
     }

     /* STATE MACHINE */
     /* ============= */

     void * STATE_MACHINE(void *arg){
           /* RUN */
           while(FOREVER) {
             //printf("seq1:%d - seq2:%d = winsize:%d\n", client_buf.seq_1, client_buf.seq_2, client_buf.seq_1-client_buf.seq_2);
             /* STATE CHECK */
             switch(state){

                 /*=============*/
                 /*STATE: CLOSED*/
                 case CLOSED:{
                     //printf("\nSTATE = CLOSED\n");
                     /*INPUT: CONNECT*/
                     if(input == EXIT){
                       //printf("EXIT()\n");
                       input = NONE;
                       state = EXITING;
                     }else if(input == CONNECT){
                       //printf("CONNECT()\n");
                         input = NONE;
                         //Send SYN
                         OUT_send_syn();
                         //GO TO: SYN_SENT
                         state = SYN_SENT;
                     }else if(input == LISTEN){
                         input = NONE;
                         state = LISTENING;
                     }
                 } break;

                 /*=============*/
                 /*STATE: SYN_SENT*/
                 case SYN_SENT:{
                     //printf("\nSTATE = SYN_SENT\n");
                     if(input == SYN_ACK){
                       printf("IN: SYN_ACK\n");
                         input = NONE;
                         //Send ACK
                         OUT_send_ack(-1);
                         //GO TO: PRE_ESTABLISHED
                         state = PRE_ESTABLISHED;
                         reset_timer(&syn_sent_timer);
                     }else{

                         if(syn_sent_timer.on == -1){
                             printf("NO SYN_ACK -> set timer\n");
                             syn_sent_timer.on = 3;
                             syn_sent_timer.start = clock();
                             syn_sent_timer.length = clock_time(10);
                         }else{
                             if(timeout(syn_sent_timer) == 1){
                                printf("NO SYN_ACK -> timeout\n");
                                 OUT_send_syn();
                                 syn_sent_timer.on--;
                             }
                             if(syn_sent_timer.on == 0){
                                 reset_timer(&syn_sent_timer);
                                 state = CLOSED;
                                 printf("CLOSING\n");
                             }
                         }
                     }
                 } break;

                 /*=============*/
                 /*STATE: PRE_ESTABLISHED*/
                 case PRE_ESTABLISHED:{
                     //printf("\nSTATE = PRE_ESTABLISHED\n");
                     if(input == SYN_ACK){
                       printf("IN: SYN_ACK\n");
                         input = NONE;
                         OUT_send_ack(-1);
                     }else{
                         if(pre_established_timer.on == -1){
                             pre_established_timer.on = 1;
                             pre_established_timer.start = clock();
                             pre_established_timer.length = clock_time(10);
                         }else{
                             if(timeout(pre_established_timer) == 1){
                                 state = ESTABLISHED_CLIENT;
                                 reset_timer(&pre_established_timer);
                             }
                         }
                     }
                 } break;

                 /*=============*/
                 /*STATE: ESTABLISHED_CLIENT*/
                 case ESTABLISHED_CLIENT:{

                   //printf("\nSTATE = ESTABLISHED_CLIENT\n");
                   if(input == CLOSE){
                     //printf("CLOSE\n");
                     input = NONE;
                     state = FIN_WAIT_1;
                     OUT_send_fin();
                   }else{

                     if(ack_buf.seq_1 < ack_buf.seq_2 || ack_buf.seq_1 > ack_buf.seq_2){
                       //printf("-----READING ACK BUF---seq_1:%d < seq_2:%d----\n", ack_buf.seq_1, ack_buf.seq_2);
                      //next packet waiting ack for < next packet to send
                       if(client_buf.seq_2 < client_buf.seq_1){
                         if(ack_buf.packet[ack_buf.seq_1].seq == client_buf.seq_2){
                           printf("RCVD: ACK [SEQ:%d]\n", client_buf.seq_2);
                           reset_timer(&client_established[client_buf.seq_2]);
                           client_buf.seq_2 = next_seq(client_buf.seq_2);
                         }
                         //sequence for the packet we recieve > next ack we waiting for
                         else if(ack_buf.packet[ack_buf.seq_1].seq > client_buf.seq_2){
                           printf("RCVD: ACK [SEQ:%d]\n", ack_buf.packet[ack_buf.seq_1].seq);
                           reset_timer(&client_established[ack_buf.packet[ack_buf.seq_1].seq]);
                           client_buf.seq_2 = next_seq(ack_buf.packet[ack_buf.seq_1].seq);
                         }
                       }
                       //next seq we waiting for > seq for next packet to send
                       else if(client_buf.seq_2 > client_buf.seq_1){
                         if(ack_buf.packet[ack_buf.seq_1].seq >= client_buf.seq_2 || ack_buf.packet[ack_buf.seq_1].seq < client_buf.seq_1){
                           printf("RCVD: ACK [SEQ:%d]\n", ack_buf.packet[ack_buf.seq_1].seq);
                           reset_timer(&client_established[ack_buf.packet[ack_buf.seq_1].seq]);
                           client_buf.seq_2 = next_seq(ack_buf.packet[ack_buf.seq_1].seq);
                         }

                       }

                       ack_buf.seq_1 = next_seq(ack_buf.seq_1);
                     }

                     //next place to load new packet to buffer > next packet to send
                     if(client_buf.seq_0 > client_buf.seq_1 && client_window() < window){

                       //printf("WINDOW: %d < %d\n", client_window(), WINDOW);
                       OUT_send_packet(client_buf.packet[client_buf.seq_1]);
                       if(client_established[client_buf.seq_1].on == -1){
                         client_established[client_buf.seq_1].on = RESEND_PACKS;
                       }else{
                         decrease_timer(&client_established[client_buf.seq_1]);
                         if(client_established[client_buf.seq_1].on == 0){
                           input = CLOSE;
                           printf("Could not reach client! Closing...\n");
                         }
                       }
                       client_established[client_buf.seq_1].start = clock();
                       client_established[client_buf.seq_1].length = clock_time(10);
                       //printf("SET TIMER: %d; ON=%d;\n", client_buf.seq_1, client_established[client_buf.seq_1].on);
                       client_buf.seq_1 = next_seq(client_buf.seq_1);
                     }


                     int i;
                     i = client_buf.seq_2;
                     while(i < client_buf.seq_1){
                       if(timeout(client_established[i]) == 1){
                         //printf("PACK_TIMEOUT: %d; ON=%d;\n", i, client_established[i].on);
                         printf("ACK TIMEOUT [SEQ: %d]\n", client_buf.packet[i].seq);
                         client_buf.seq_1 = i;
                         break;
                       }
                       i++;
                     }


                   }
                 } break;

                 /*=============*/
                 /*STATE: LISTENING*/
                 case LISTENING:{
                     //printf("\nSTATE = LISTENING\n");
                     if(input == SYN){
                       printf("IN: SYN\n");
                         input = NONE;
                         OUT_send_syn_ack();
                         state = SYN_RECIEVED;
                     }else if(input == CLOSE){
                       //printf("LISTENING >> IN: CLOSE\n");
                         input = NONE;
                         state = CLOSE;
                     }
                 } break;

                 /*=============*/
                 /*STATE: SYN_RECIEVED*/
                 case SYN_RECIEVED:{
                     //printf("\nSTATE = SYN_RECIEVED\n");
                     if(input == ACK){
                       printf("IN: ACK\n");
                       input = NONE;
                       state = ESTABLISHED_SERVER;
                       printf("ESTABLISHED_SERVER\n");
                       reset_timer(&syn_recieved_timer);
                     }else if(input == RESET){
                       printf("RESET\n");
                       input = NONE;
                       state = LISTENING;
                       reset_timer(&syn_recieved_timer);
                     }else{
                       if(syn_recieved_timer.on == -1){
                         syn_recieved_timer.on = 3;
                         syn_recieved_timer.start = clock();
                         syn_recieved_timer.length = clock_time(10);
                       }else{
                         if(timeout(syn_recieved_timer) == 1){
                             OUT_send_syn_ack();
                             syn_recieved_timer.on--;
                         }
                         if(syn_recieved_timer.on == 0){
                           state = LISTEN;
                           reset_timer(&syn_recieved_timer);
                         }
                       }
                     }
                 } break;

                 /*=============*/
                 /*STATE: ESTABLISHED_SERVER*/
                 case ESTABLISHED_SERVER:{

                     //printf("\nSTATE = ESTABLISHED_SERVER\n");
                   if(input == FIN){
                     printf("IN: FIN\n");
                     input = NONE;
                     OUT_send_ack(-1);
                     state = CLOSE_WAIT;
                   }else{
                     if(re_ack == 1 && first_ack == 0){
                       //printf("ACK IS RE_ACK");
                       OUT_send_ack(prev_ack(server_buf.seq_1));
                       re_ack = 0;
                     }
                     if(server_buf.seq_1 < server_buf.seq_2){
                       if(server_buf.packet[server_buf.seq_1].seq == server_buf.seq_1){

                         //printf("ESTABLISHED_SERVER >> new packet\n");
                         (*process_data)(server_buf.packet[server_buf.seq_1].data);
                         printf("RCVD: PACKET [SEQ: %d, DATA: %.*s];\n", server_buf.packet[server_buf.seq_1].seq, PACKET_DATA_SIZE, server_buf.packet[server_buf.seq_1].data);
                         //printf("SENT: ACK [SEQ: %d]\n", server_buf.packet[server_buf.seq_1].seq);
                         OUT_send_ack(server_buf.packet[server_buf.seq_1].seq);
                         server_buf.seq_1 = next_seq(server_buf.seq_1);
                         first_ack = 0;
                       }
                     }
                   }
                 } break;

                 /*=============*/
                 /*STATE: FIN_WAIT_1 */
                 case FIN_WAIT_1:{
                     //printf("\nSTATE = FIN_WAIT_1\n");
                   if(input == ACK){
                     printf("IN: ACK\n");
                     input = NONE;
                     state = FIN_WAIT_2;
                   }else if(input == FIN_ACK){
                     printf("IN: FIN_ACK\n");
                     input = NONE;
                     OUT_send_ack(-1);
                     state = TIME_WAIT;
                   }else if(input == FIN){
                     printf("IN: FIN\n");
                     input == NONE;
                     OUT_send_ack(-1);
                     state = CLOSING;
                   }else{
                     if(fin_wait_1_timer.on == -1){
                       fin_wait_1_timer.on = 3;
                       fin_wait_1_timer.start = clock();
                       fin_wait_1_timer.length = clock_time(10);
                     }else{
                       if(timeout(fin_wait_1_timer) == 1){
                         decrease_timer(&fin_wait_1_timer);
                         OUT_send_fin();
                       }
                       if(fin_wait_1_timer.on == 0){
                         state = TIME_WAIT;
                       }
                     }
                   }

                 } break;

                 /*=============*/
                 /* STATE: FIN_WAIT_2 */
                 case FIN_WAIT_2:{
                    // printf("\nSTATE = FIN_WAIT_2\n");
                   if(input == FIN){
                     printf("IN: FIN\n");
                     input = NONE;
                     OUT_send_ack(-1);
                     state = TIME_WAIT;
                   }
                 } break;

                 /*=============*/
                 /*STATE: TIME_WAIT*/
                 case TIME_WAIT:{
                     //printf("\nSTATE = TIME_WAIT\n");
                   if(input == FIN){
                     printf("IN: FIN\n");
                     input = NONE;
                     OUT_send_ack(-1);
                   }else if(input == FIN_ACK){
                     printf("IN: FIN_ACK\n");
                     input = NONE;
                     OUT_send_ack(-1);
                   }else{
                     if(time_wait_timer.on == -1){
                       time_wait_timer.on = 1;
                       time_wait_timer.start = clock();
                       time_wait_timer.length = clock_time(10);
                     }else{
                       if(timeout(time_wait_timer) == 1){
                         state = CLOSED;
                         printf("CLOSING\n");
                       }
                     }
                   }
                 } break;

                 /*=============*/
                 /*STATE: CLOSING*/
                 case CLOSING:{
                    // printf("\nSTATE = CLOSING\n");
                   if(input == ACK){
                     printf("ACK\n");
                     input = NONE;
                     state = TIME_WAIT;
                   }else{
                     if(closing_timer.on == -1){
                       closing_timer.on = 3;
                       closing_timer.start = clock();
                       closing_timer.length = clock_time(10);
                     }else{
                       if(timeout(closing_timer) == 1){
                         decrease_timer(&closing_timer);
                         OUT_send_ack(-1);
                       }
                       if(closing_timer.on == 0){
                         closing_timer.on = -1;
                         state = TIME_WAIT;
                       }
                     }
                   }
                 } break;

                 /*=============*/
                 /*STATE: CLOSE_WAIT*/
                 case CLOSE_WAIT:{
                     //printf("\nSTATE = CLOSE_WAIT\n");
                   if(input == FIN){
                     printf("IN: FIN\n");
                     input = NONE;
                     OUT_send_ack(-1);
                   }else if(close_wait_timer.on == -1){
                     close_wait_timer.on = 1;
                     close_wait_timer.start = clock();
                     close_wait_timer.length = clock_time(10);
                   }else{
                     if(timeout(close_wait_timer) == 1){
                       close_wait_timer.on = -1;
                       state = LAST_ACK;
                       OUT_send_fin();
                     }
                   }
                 } break;

                 /*=============*/
                 /*STATE: LAST_ACK*/
                 case LAST_ACK:{
                   //printf("\nSTATE = LAST_ACK\n");
                   if(input == ACK){
                     printf("IN: ACK\n");
                     input = NONE;
                     state = CLOSED;
                     printf("CLOSING\n");
                   }else{
                     if(last_ack_timer.on == -1){
                       last_ack_timer.on = 3;
                       last_ack_timer.start = clock();
                       last_ack_timer.length = clock_time(10);
                     }else{
                       if(timeout(last_ack_timer) == 1){
                         decrease_timer(&last_ack_timer);
                         OUT_send_fin();
                       }
                       if(last_ack_timer.on == 0){
                         state = CLOSED;
                         printf("CLOSING\n");
                         reset_timer(&last_ack_timer);
                       }
                     }
                   }
                 } break;

                 /*=============*/
                 /*STATE: EXITING*/
                 case EXITING:{
                   printf("\nSTATE = EXITING\n");
                   close(server_socket);
                   return NULL;
                 } break;
             }
             usleep(1000*100);
           }
     }
