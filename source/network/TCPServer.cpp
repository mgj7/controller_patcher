/****************************************************************************
 * Copyright (C) 2016,2017 Maschell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include "TCPServer.hpp"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <utils/logger.h>

#define wiiu_errno (*__gh_errno_ptr())

CThread * TCPServer::pThread = NULL;
TCPServer * TCPServer::instance = NULL;

TCPServer::TCPServer(s32 port){
    this->sockfd = -1;
    this->clientfd = -1;
    memset(&(this->sock_addr),0,sizeof(this->sock_addr));
    TCPServer::AttachDetach(DETACH);
	StartTCPThread(this);
}

TCPServer::~TCPServer(){
    CloseSockets();
    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("Thread will be closed\n"); }
    TCPServer::AttachDetach(DETACH);
    exitThread = 1;
    if(TCPServer::pThread != NULL){
        if(HID_DEBUG){ DEBUG_FUNCTION_LINE("Deleting it!\n"); }
        delete TCPServer::pThread;
    }
    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("Thread done\n"); }
    TCPServer::pThread = NULL;
}

void TCPServer::CloseSockets(){
    if (this->sockfd != -1){
        socketclose(this->sockfd);
    }
    if (this->clientfd != -1){
        socketclose(this->clientfd);
    }
    this->sockfd = -1;
    this->clientfd = -1;
}

void TCPServer::StartTCPThread(TCPServer * server){
    s32 priority = 28;
    if(OSGetTitleID() == 0x00050000101c9300 || //The Legend of Zelda Breath of the Wild JPN
       OSGetTitleID() == 0x00050000101c9400 || //The Legend of Zelda Breath of the Wild USA
       OSGetTitleID() == 0x00050000101c9500 || //The Legend of Zelda Breath of the Wild EUR
       OSGetTitleID() == 0x00050000101c9b00 || //The Binding of Isaac: Rebirth EUR
       OSGetTitleID() == 0x00050000101a3c00){  //The Binding of Isaac: Rebirth USA
        priority = 10;
        DEBUG_FUNCTION_LINE("This game needs higher thread priority. We set it to %d\n",priority);
    }
    TCPServer::pThread = CThread::create(TCPServer::DoTCPThread, (void*)server, CThread::eAttributeAffCore2,priority);
    TCPServer::pThread->resumeThread();
}

void TCPServer::AttachDetach(s32 attach){
    if(HID_DEBUG){
        if(attach){
            DEBUG_FUNCTION_LINE("Network Attach\n");
        }else{
            DEBUG_FUNCTION_LINE("Network Detach\n");
        }
    }

    for(s32 i= 0;i< gHIDMaxDevices;i++){
        for(s32 j= 0;j< HID_MAX_PADS_COUNT;j++){
            if(gNetworkController[i][j][NETWORK_CONTROLLER_ACTIVE] > 0){
                DEBUG_FUNCTION_LINE("Found a registered pad in deviceslot %d and padslot %d! Lets detach it.\n",i,j);
                HIDDevice device;
                memset(&device,0,sizeof(device));

                device.interface_index = 0;
                device.vid = gNetworkController[i][j][NETWORK_CONTROLLER_VID];
                device.pid = gNetworkController[i][j][NETWORK_CONTROLLER_PID];
                device.handle = gNetworkController[i][j][NETWORK_CONTROLLER_HANDLE];
                device.max_packet_size_rx = 8;
                ControllerPatcherHID::externAttachDetachCallback(&device,attach);
                memset(gNetworkController[i][j],0,sizeof(gNetworkController[i][j]));
            }
        }
    }

    if(HID_DEBUG){
        if(attach){
            DEBUG_FUNCTION_LINE("Network Attach DONE!\n");
        }else{
            DEBUG_FUNCTION_LINE("Network Detach DONE!\n");
        }
    }
}

void TCPServer::DetachAndDelete(){
    TCPServer::AttachDetach(DETACH);
    memset(&gNetworkController,0,sizeof(gNetworkController));
}

s32 TCPServer::RunTCP(){
    s32 ret;
	while (1) {
        if(exitThread) break;
		ret = ControllerPatcherNet::checkbyte(clientfd);
		if (ret < 0) {
            if(wiiu_errno != 6) return ret;
            os_usleep(1000);
			continue;
		}
        //DEBUG_FUNCTION_LINE("got byte from tcp! %01X\n",ret);
		switch (ret) {
            case WIIU_CP_TCP_ATTACH: { /*attach */
                if(gUsedProtocolVersion >= WIIU_CP_TCP_HANDSHAKE_VERSION_1){
                    s32 handle;
                    ret = ControllerPatcherNet::recvwait(clientfd, &handle, 4);
                    if(ret < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: recvwait handle\n",WIIU_CP_TCP_ATTACH);
                        return ret;
                    }
                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("got handle %d\n",handle); }
                    u16 vid = 0;
                    u16 pid = 0;
                    ret = ControllerPatcherNet::recvwait(clientfd, &vid, 2);
                    if(ret < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: recvwait vid\n",WIIU_CP_TCP_ATTACH);
                        return ret;
                    }
                   if(HID_DEBUG){ DEBUG_FUNCTION_LINE("got vid %04X\n",vid); }

                    ret = ControllerPatcherNet::recvwait(clientfd, &pid, 2);
                    if(ret < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: recvwait pid\n",WIIU_CP_TCP_ATTACH);
                        return ret;
                    }
                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("got pid %04X\n",pid); }
                    HIDDevice device;
                    memset(&device,0,sizeof(device));
                    device.handle = handle;
                    device.interface_index = 0;
                    device.vid = SWAP16(vid);
                    device.pid = SWAP16(pid);
                    device.max_packet_size_rx = 8;

                    my_cb_user * user  = NULL;
                    ControllerPatcherHID::externAttachDetachCallback(&device,1);
                    if((ret = ControllerPatcherUtils::getDataByHandle(handle,&user)) < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: getDataByHandle(%d,%08X).\n",WIIU_CP_TCP_ATTACH,handle,&user);
                        DEBUG_FUNCTION_LINE("Error in %02X: Config for the controller is missing.\n",WIIU_CP_TCP_ATTACH);
                        if((ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_ATTACH_CONFIG_NOT_FOUND) < 0)){
                            DEBUG_FUNCTION_LINE("Error in %02X: Sending the WIIU_CP_TCP_ATTACH_CONFIG_NOT_FOUND byte failed. Error: %d.\n",WIIU_CP_TCP_ATTACH,ret);
                        }
                        return -1;
                    }
                    if((ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_ATTACH_CONFIG_FOUND) < 0)){
                        DEBUG_FUNCTION_LINE("Error in %02X: Sending the WIIU_CP_TCP_ATTACH_CONFIG_FOUND byte failed. Error: %d.\n",WIIU_CP_TCP_ATTACH,ret);
                        return ret;
                    }
                    if(user != NULL){
                        if((ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_ATTACH_USER_DATA_OKAY) < 0)){
                            DEBUG_FUNCTION_LINE("Error in %02X: Sending the WIIU_CP_TCP_ATTACH_USER_DATA_OKAY byte failed. Error: %d.\n",WIIU_CP_TCP_ATTACH,ret);
                            return ret;
                        }

                        ret = ControllerPatcherNet::sendwait(clientfd,&user->slotdata.deviceslot,2);
                        if(ret < 0){
                            DEBUG_FUNCTION_LINE("Error in %02X: sendwait slotdata: %04X\n",WIIU_CP_TCP_ATTACH,user->slotdata.deviceslot);
                            return ret;
                        }
                        ret = ControllerPatcherNet::sendwait(clientfd,&user->pad_slot,1);
                        if(ret < 0){
                            DEBUG_FUNCTION_LINE("Error in %02X: sendwait pad_slot: %04X\n",WIIU_CP_TCP_ATTACH,user->pad_slot);
                            return ret;
                        }
                    }else{
                        DEBUG_FUNCTION_LINE("Error in %02X: invalid user data.\n",WIIU_CP_TCP_ATTACH);
                        if((ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_ATTACH_USER_DATA_BAD) < 0)){
                            DEBUG_FUNCTION_LINE("Error in %02X: Sending the WIIU_CP_TCP_ATTACH_USER_DATA_BAD byte failed. Error: %d.\n",WIIU_CP_TCP_ATTACH,ret);
                            return ret;
                        }
                        return -1;
                        break;
                    }

                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("attachted to device slot: %d , pad slot is: %d\n",user->slotdata.deviceslot,user->pad_slot); }

                    gNetworkController[user->slotdata.deviceslot][user->pad_slot][NETWORK_CONTROLLER_VID] = device.vid;
                    gNetworkController[user->slotdata.deviceslot][user->pad_slot][NETWORK_CONTROLLER_PID] = device.pid;
                    gNetworkController[user->slotdata.deviceslot][user->pad_slot][NETWORK_CONTROLLER_ACTIVE] = 1;
                    gNetworkController[user->slotdata.deviceslot][user->pad_slot][NETWORK_CONTROLLER_HANDLE] = handle;

                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("handle %d connected! vid: %02X pid: %02X deviceslot %d, padslot %d\n",handle,vid,pid,user->slotdata.deviceslot,user->pad_slot); }
                    break;
                }
                break;
            }
            case WIIU_CP_TCP_DETACH: { /*detach */
                if(gUsedProtocolVersion >= WIIU_CP_TCP_HANDSHAKE_VERSION_1){
                    s32 handle;
                    ret = ControllerPatcherNet::recvwait(clientfd, &handle, 4);
                    if(ret < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: recvwait handle\n",WIIU_CP_TCP_DETACH);
                        return ret;
                        break;
                    }

                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("got detach for handle: %d\n",handle); }
                    my_cb_user * user  = NULL;
                    if(ControllerPatcherUtils::getDataByHandle(handle,&user) < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: getDataByHandle(%d,%08X).\n",WIIU_CP_TCP_DETACH,handle,&user);
                        return -1;
                        break;
                    }
                    if(user == NULL){
                        DEBUG_FUNCTION_LINE("Error in %02X: invalid user data.\n",WIIU_CP_TCP_DETACH);
                        return -1;
                        break;
                    }
                    s32 deviceslot = user->slotdata.deviceslot;
                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("device slot is: %d , pad slot is: %d\n",deviceslot,user->pad_slot); }

                    DeviceVIDPIDInfo vidpid;
                    s32 result;
                    if((result = ControllerPatcherUtils::getVIDPIDbyDeviceSlot(deviceslot,&vidpid)) < 0){
                        DEBUG_FUNCTION_LINE("Error in %02X: Couldn't find a valid VID/PID for device slot %d. Error: %d\n",WIIU_CP_TCP_DETACH,deviceslot,ret);
                        return -1;
                        break;
                    }

                    HIDDevice device;
                    memset(&device,0,sizeof(device));
                    device.handle = handle;
                    device.interface_index = 0;
                    device.vid = SWAP16(vidpid.vid);
                    device.pid = SWAP16(vidpid.pid);
                    device.max_packet_size_rx = 14;

                    ControllerPatcherHID::externAttachDetachCallback(&device,DETACH);
                    memset(gNetworkController[deviceslot][user->pad_slot],0,sizeof(gNetworkController[deviceslot][user->pad_slot]));
                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("handle %d disconnected!\n",handle); }
                    break;
                }
                break;
            }
            case WIIU_CP_TCP_PING: { /*ping*/
                if(gUsedProtocolVersion >= WIIU_CP_TCP_HANDSHAKE_VERSION_1){
                    if(HID_DEBUG){ DEBUG_FUNCTION_LINE("Got Ping, sending now a Pong\n"); }
                    s32 ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_PONG);
                    if(ret < 0){ DEBUG_FUNCTION_LINE("Error in %02X: sendbyte PONG\n"); return -1;}

                    break;
                }
                break;
            }
            default:
                return -1;
                break;
		}
	}
	return 0;
}

void TCPServer::ErrorHandling(){
    CloseSockets();
    os_usleep(1000*1000*2);
}

void TCPServer::DoTCPThreadInternal(){
    s32 ret;
    s32 len;
    while (1) {
        if(exitThread) break;
        memset(&(this->sock_addr),0,sizeof(sock_addr));
		sock_addr.sin_family = AF_INET;
		sock_addr.sin_port = DEFAULT_TCP_PORT;
		sock_addr.sin_addr.s_addr = 0;

		this->sockfd = ret = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(ret == -1){ ErrorHandling(); continue;}
        s32 enable = 1;

        setsockopt(this->sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

		ret = bind(this->sockfd, (sockaddr *)&sock_addr, 16);
		if(ret < 0) { ErrorHandling(); continue;}
		ret = listen(this->sockfd, 1);
		if(ret < 0){ ErrorHandling(); continue;}

        do{
            if(HID_DEBUG){ DEBUG_FUNCTION_LINE("Waiting for a connection\n"); }
            if(exitThread) break;
            len = 16;

            /**
                Handshake
                1. At first this server sends his protocol version
                2. The network clients answers with his preferred version (which needs to be equals or lower the version this server sent him) or an abort command.
                    3a. If the client sent a abort, close the connection and wait for another connection
                    3b. If the client sent his highest supported version, the server confirm that he is able to use this version (by sending the version back) or sending a abort command to disconnect.
            **/

            clientfd = ret = accept(sockfd, (sockaddr *)&(sock_addr), &len);

            if(ret == -1){ ErrorHandling(); break;}
            DEBUG_FUNCTION_LINE("TCP Connection accepted! Sending my protocol version: %d (0x%02X)\n", (WIIU_CP_TCP_HANDSHAKE - WIIU_CP_TCP_HANDSHAKE_VERSION_1)+1,WIIU_CP_TCP_HANDSHAKE);

            gUDPClientip = sock_addr.sin_addr.s_addr;
            UDPClient::createInstance();

            s32 ret;
            ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_HANDSHAKE); //Hey I'm a WiiU console!
            if(ret < 0){ DEBUG_FUNCTION_LINE("Error sendbyte: %02X\n",WIIU_CP_TCP_HANDSHAKE); ErrorHandling(); break;}

            u8 clientProtocolVersion = ControllerPatcherNet::recvbyte(clientfd);
            if(ret < 0){ DEBUG_FUNCTION_LINE("Error recvbyte: %02X\n",WIIU_CP_TCP_HANDSHAKE); ErrorHandling(); break;}

            if(clientProtocolVersion == WIIU_CP_TCP_HANDSHAKE_ABORT){
                 DEBUG_FUNCTION_LINE("The network client wants to abort.\n");
                 ErrorHandling(); break;
            }

            DEBUG_FUNCTION_LINE("received protocol version: %d (0x%02X)\n",(clientProtocolVersion - WIIU_CP_TCP_HANDSHAKE_VERSION_1)+1,clientProtocolVersion);

            if(clientProtocolVersion >= WIIU_CP_TCP_HANDSHAKE_VERSION_MIN && clientProtocolVersion <= WIIU_CP_TCP_HANDSHAKE_VERSION_MAX){
                DEBUG_FUNCTION_LINE("We support this protocol version. Let's confirm it to the network client.\n");
                gUsedProtocolVersion = clientProtocolVersion;
                ret = ControllerPatcherNet::sendbyte(clientfd, clientProtocolVersion);
                if(ret < 0){ DEBUG_FUNCTION_LINE("Error sendbyte: %02X\n",clientProtocolVersion); ErrorHandling(); break;}
            }else{
                DEBUG_FUNCTION_LINE("We don't support this protocol version. We need to abort =(.\n");
                ret = ControllerPatcherNet::sendbyte(clientfd, WIIU_CP_TCP_HANDSHAKE_ABORT);
                ErrorHandling(); break;
            }

            DEBUG_FUNCTION_LINE("Handshake done! Success!\n");

            TCPServer::DetachAndDelete(); //Clear connected controller
            RunTCP();

            if(clientfd != -1){
                socketclose(clientfd);
            }
            clientfd = -1;
        }while(0);
        DEBUG_FUNCTION_LINE("Connection closed\n");
        gUDPClientip = 0;
        UDPClient::destroyInstance();
        TCPServer::DetachAndDelete(); //Clear connected controller
        CloseSockets();
		continue;
	}

}

void TCPServer::DoTCPThread(CThread *thread, void *arg){
    TCPServer * args = (TCPServer * )arg;
    return args->DoTCPThreadInternal();
}
