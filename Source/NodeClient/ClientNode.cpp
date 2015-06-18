/*
This project is licensed under the GNU GPL 2.0 license. Please respect that.

Initial author: (https://github.com/)Sokie
Started: 11-06-2015
Notes
This handles connection to a server node
*/

#include "..\STDInclude.h"

#define waitTimeout 5
namespace Nodes
{
	hAddress ClientNode::serverNode = hAddress::hAddress();
	uint32_t ClientNode::sequenceID = 0;
	bool ClientNode::isSNodeConnected = false;
	HANDLE ClientNode::sNodeDiscoveryThread;
	HANDLE ClientNode::sPacketReceiverThread;
	std::mutex ClientNode::mutex;
	std::unordered_map<uint32_t, std::shared_ptr<ByteBuffer>> ClientNode::pendingData;

	DWORD _stdcall ClientNode::NodeDiscoverySender(void  *lparam)
	{
		uint16_t port = 20000;
		Network::SocketManager::Create_UDP(&port, false);

		char *recvBuffer = (char *)malloc(sizeof(char) * 1024);
		while (true){
			DBGPrint("Searching for sNode");

			Network::PingPacket *pingPacket = new Network::PingPacket();
			pingPacket->ClientID = Global::Steam_UserID;
			pingPacket->isAnonymous = true;
			pingPacket->isAuthenticated = false;
			pingPacket->SessionID = rand();
			pingPacket->username = Global::Steam_Username;
			SetNetworkData(pingPacket, HNPingRequest);

			ByteBuffer pingBuffer = ByteBuffer::ByteBuffer();
			pingPacket->Serialize(&pingBuffer);

			Network::SocketManager::Send_UDPBroadcast(31337, pingBuffer.GetLength(), pingBuffer.GetBuffer<void>());

			delete pingPacket;

			int len = Network::SocketManager::Receive_UDP(port, 1024, recvBuffer, &serverNode);
			if (len > 0){
				serverNode.Port = 31337;
				DBGPrint("Found sNode, stopping...");
				isSNodeConnected = true;
				sPacketReceiverThread = CreateThread(NULL, NULL, NodePacketReceiver, NULL, NULL, NULL);

				free(recvBuffer);
				break;
			}
			Sleep(2000);
		}

		return 0;
	}

	void ClientNode::SetNetworkData(Network::NetworkPacket *inPacket, EventType eventType){
		inPacket->ApplicationID = Global::Steam_AppID;
		inPacket->eventType = eventType;
		inPacket->SequenceID = GetSequence();
		inPacket->TimeStamp = time(NULL);
	}

	DWORD _stdcall ClientNode::NodePacketReceiver(void  *lparam)
	{
		uint16_t port = 20000;

		char *recvBuffer = (char *)malloc(sizeof(char) * 1024);
		hAddress sender = hAddress::hAddress();
		while (true){

			int32_t len = Network::SocketManager::Receive_UDP(port, 1024, recvBuffer, &sender);
			if (len > 0){

				ByteBuffer *packetBuffer = new ByteBuffer(len, (void*)recvBuffer);
				Network::NetworkPacket *packet = new Network::NetworkPacket();
				packet->Deserialize(packetBuffer);
				mutex.lock();
				pendingData[packet->SequenceID] = std::shared_ptr<ByteBuffer>(packetBuffer);
				delete packet;
				mutex.unlock();
			}
			Sleep(100);
		}

		return 0;
	}

	bool ClientNode::InitializeNode(){
		sequenceID = 0;
		sNodeDiscoveryThread = CreateThread(NULL, NULL, NodeDiscoverySender, NULL, NULL, NULL);
		return true;
	}

	uint32_t ClientNode::GetSequence(){
		mutex.lock();
		++sequenceID;
		mutex.unlock();
		return sequenceID;
	}

	int32_t ClientNode::GetFriendCount(int32_t iFriendFlags)
	{
		if (!isSNodeConnected){
			return 0;
		}
		Network::NetworkPacket *packet = new Network::NetworkPacket();

		SetNetworkData(packet, HNFriendCountRequest);

		ByteBuffer packetBuffer = ByteBuffer::ByteBuffer();
		packet->Serialize(&packetBuffer);
		Network::SocketManager::Send_UDP(&serverNode, packetBuffer.GetLength(), packetBuffer.GetBuffer<void>());

		int32_t friendsCount = 0;
		time_t t1 = time(0);
		while (true){
			if (difftime(time(0), t1) > waitTimeout){
				break;
			}
			mutex.lock();
			std::unordered_map<uint32_t, std::shared_ptr<ByteBuffer>>::const_iterator find = pendingData.find(packet->SequenceID);
			mutex.unlock();

			if (find == pendingData.end()){
				continue;
			}else{
				std::shared_ptr<ByteBuffer> packetData = find->second;
				
				if (packetData == NULL){
					return 0;
				}

				ByteBuffer *bf = packetData.get();
				if (packetData->GetPosition() > 0){
					packetData->Rewind();
				}

				Network::FriendCountPacket friendCount = Network::FriendCountPacket();
				friendCount.Deserialize(bf);

				friendsCount = friendCount.friendsCount;

				pendingData.erase(find);
				delete packet;

				return friendsCount;
			}
		}

		delete packet;

		return friendsCount;
	}

	uint64_t ClientNode::GetFriendByIndex(int32_t iFriend, int32_t iFriendFlags){
		if (!isSNodeConnected){
			return 0x1100001DEADC0DE;
		}
		Network::NetworkPacket *packet = new Network::NetworkPacket();

		SetNetworkData(packet, HNFriendAtIndexRequest);

		ByteBuffer packetBuffer = ByteBuffer::ByteBuffer();
		packet->Serialize(&packetBuffer);

		packetBuffer.WriteInt32(iFriend);

		Network::SocketManager::Send_UDP(&serverNode, packetBuffer.GetLength(), packetBuffer.GetBuffer<void>());

		uint64_t steamID = 0;
		time_t t1 = time(0);
		while (true){
			if (difftime(time(0), t1) > waitTimeout){
				break;
			}

			mutex.lock();
			std::unordered_map<uint32_t, std::shared_ptr<ByteBuffer>>::const_iterator find = pendingData.find(packet->SequenceID);
			mutex.unlock();

			if (find == pendingData.end()){
				continue;
			}
			else{
				std::shared_ptr<ByteBuffer> packetData = find->second;

				if (packetData == NULL){
					return 0;
				}

				ByteBuffer *bf = packetData.get();
				if (packetData->GetPosition() > 0){
					packetData->Rewind();
				}

				Network::FriendAtIndex friendIndex = Network::FriendAtIndex();
				friendIndex.Deserialize(bf);
				steamID = friendIndex.friendSteamID;

				pendingData.erase(find);

				delete packet;
				return steamID;
			}
		}

		delete packet;

		return steamID;
	}
}