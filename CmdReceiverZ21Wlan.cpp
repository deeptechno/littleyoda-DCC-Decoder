/*
 * CmdReceiverZ21Wlan.cpp
 *
 *  Created on: 18.08.2016
 *      Author: sven
 */

#include "CmdReceiverZ21Wlan.h"
#include <stdint.h>
#include "Controller.h"
#include "Logger.h"
#include "Consts.h"
#include "Utils.h"

CmdReceiverZ21Wlan::CmdReceiverZ21Wlan(Controller* c, const char* ip) :
		CmdReceiverBase(c) {
	Logger::getInstance()->addToLog("Starting Z21 Wlan Receiver ...");
	if (ip == NULL) {
		z21Server = new IPAddress(192, 168, 0, 111);
	} else {
		z21Server = new IPAddress();
		z21Server->fromString(ip);

	}
	Logger::getInstance()->addToLog("Using: " + String(z21Server->toString()));
	udp = new WiFiUDP();
	udp->begin(localPort);
	enableBroadcasts();
}

int CmdReceiverZ21Wlan::loop() {
	// Check for UDP
	int cb = udp->parsePacket();
	if (cb != 0) {
		doReceive(cb);
	}
	long int time = millis();
	if ((timeout > 0) && ((time - timeout) > emergencyStopTimeout)) {
		Logger::getInstance()->addToLog("Z21 wlan Timeout");
		emergencyStop();
		timeout = 0;
	}

	// Scheduler for Requests
	if (time - lastTime > (emergencyStopTimeout / 4)) {
		lastTime = time;
		if (loopStatus == -3) {
			sendXGetStatus();
		} else if (loopStatus == -2) {
			enableBroadcasts();
		} else if (loopStatus == -1) {
			sendCfg12Request();
		} else {
			ActionBase::requestInfo* ri = requestList->get(loopStatus);
			if (ri->art == ActionBase::requestInfo::LOCO) {
				requestLocoInfo(ri->id);
			} else if (ri->art == ActionBase::requestInfo::TURNOUT) {
				requestTurnoutInfo(ri->id);
			}
		}
		loopStatus++;
		if ((requestList == NULL && loopStatus == 0)
				|| (requestList != NULL && loopStatus == requestList->size())) {
			loopStatus = firstLoopStatus;
		}
	}
	return 2;

}

void CmdReceiverZ21Wlan::handleTurnout() {
	// Check if this is a message for us
	int id = packetBuffer[5] << 8 | packetBuffer[6];
	int status = packetBuffer[7];
	// see z21 documention, why this hack is neccessary
	int output = -1;
	if (status == 1) {
		output = 0;
	} else if (status == 2) {
		output = 1;
	}

	controller->notifyTurnout(id, output, 1);
}

void CmdReceiverZ21Wlan::handleDCCSpeed(unsigned int locoid) {
	unsigned int fahrstufen = packetBuffer[7] & 7;
	if (fahrstufen == 0) {
		fahrstufen = 14;
	} else if (fahrstufen == 2) {
		fahrstufen = 28;
	} else if (fahrstufen == 4) {
		fahrstufen = 128;
	}

	int richtung = (packetBuffer[8] & 128) == 0 ? -1 : 1;
	unsigned int v = (packetBuffer[8] & 127);
	// Adjust to match NmraDCC Schema
	if (v == 0) {
		v = Consts::SPEED_STOP;
	} else if (v == 1) {
		v = Consts::SPEED_EMERGENCY;
	}
	controller->notifyDCCSpeed(locoid, v, richtung, fahrstufen, 1);
}

void CmdReceiverZ21Wlan::handleFunc(unsigned int locoid) {
	unsigned long func = (((packetBuffer[9] & 16) > 0) ? 1 : 0)
			+ ((packetBuffer[9] & 15) << 1) + ((packetBuffer[10]) << 5)
			+ ((packetBuffer[11]) << (8 + 5))
			+ ((packetBuffer[12]) << (16 + 5));
	controller->notifyDCCFun(locoid, 0, 29, func, 1);
}

void CmdReceiverZ21Wlan::doReceive(int cb) {
	// TODO Handle Packets with multiple Blocks in one UDP Paket
	// TODO Read the first two bytes to determine the size a
	if (cb > packetBufferSize) {
		cb = packetBufferSize;
	}
	udp->read(packetBuffer, cb);
	resetTimeout();

	boolean getSerialNumber = cb >= 8 && packetBuffer[0] == 0x08
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x10
			&& packetBuffer[3] == 0x00;
	if (getSerialNumber) {
		return;
	}

	// LAN_X_STATUS_CHANGED
	boolean statusChanged = cb >= 8 && packetBuffer[0] == 0x08
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0x62
			&& packetBuffer[5] == 0x22;
	if (statusChanged) {
		// Test if the status has been changed
		if (packetBuffer[6] == lastZ21Status) {
			return;
		}
		//csEmergencyStop
		if ((packetBuffer[6] & 0x01) > 0) {
			Logger::getInstance()->addToLog("Z21: csEmergencyStop");
			emergencyStop();
		}
		//csTrackVoltageOff
		if ((packetBuffer[6] & 0x02) > 0) {
			Logger::getInstance()->addToLog("Z21: csTrackVoltageOff");
			emergencyStop();
		}
		//csShortCircuit
		if ((packetBuffer[6] & 0x04) > 0) {
			Logger::getInstance()->addToLog("Z21: csShortCircuit");
			Serial.println("Short Circuit");
			// TODO
		}
		//csProgrammingModeActive
		if ((packetBuffer[6] & 0x20) > 0) {
			Logger::getInstance()->addToLog("Z21: csProgrammingModeActive");
			Serial.println("ProgrammingMode");
			// TODO
		}
		lastZ21Status = packetBuffer[6];
		return;
	}

	// Check if this is a TURNOUT_INFO
	boolean turnOutInfoPaket = cb >= 9 && packetBuffer[0] == 0x09
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0x43;
	if (turnOutInfoPaket) {
		handleTurnout();
		return;
	}

	boolean loco_info = cb >= 7 && packetBuffer[0] >= 8
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0xEF;
	if (loco_info) {
		unsigned int locoid = ((packetBuffer[5] & 0x3f) << 8) + packetBuffer[6];
		handleDCCSpeed(locoid);
		handleFunc(locoid);
		return;
	}

	// LAN_X_BC_TRACK_POWER_OFF
	boolean poweroff = cb >= 7 && packetBuffer[0] == 0x07
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0x61
			&& packetBuffer[5] == 0x00 && packetBuffer[6] == 0x61;
	if (poweroff) {
		emergencyStop();
		return;
	}

	// LAN_X_BC_STOPPED
	boolean emergencystop = cb >= 7 && packetBuffer[0] == 0x07
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0x81
			&& packetBuffer[5] == 0x00 && packetBuffer[6] == 0x81;
	if (emergencystop) {
		emergencyStop();
		return;
	}

	// LAN_X_BC_TRACK_POWER_ON
	boolean poweron = cb >= 7 && packetBuffer[0] == 0x07
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x40
			&& packetBuffer[3] == 0x00 && packetBuffer[4] == 0x61
			&& packetBuffer[5] == 0x01 && packetBuffer[6] == 0x60;
	if (poweron) {
		// todo
		return;
	}

	boolean cfg12 = cb >= 0xE && packetBuffer[0] == 0x0E
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x12
			&& packetBuffer[3] == 0x00;

	if (cfg12) {
		int offset = 0;
		if ((packetBuffer[11] & 4) == 0) {
			offset  = 0;
		} else {
			offset = 4;
		}
		if (turnoutOffset != offset) {
			Logger::getInstance()->addToLog("Turnout Offet: " + String(offset));
			turnoutOffset = offset;
		}
		return;
	}
	boolean cfg16 = cb >= 0x14 && packetBuffer[0] == 0x14
			&& packetBuffer[1] == 0x00 && packetBuffer[2] == 0x16
			&& packetBuffer[3] == 0x00;
	if (cfg16) {
		return;
	}
	printPacketBuffer(cb);
}

/**
 * Fragt den Weichenstatus der Weiche bei der Z21 an
 */
void CmdReceiverZ21Wlan::requestTurnoutInfo(int addr) {
	if (addr == -1) {
		return;
	}
	//Serial.println("Sending Turnout-Info Request");
	memset(packetBuffer, 0, packetBufferSize);

	packetBuffer[0] = 0x08;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x40;
	packetBuffer[3] = 0x00;

	packetBuffer[4] = 0x43;
	packetBuffer[5] = addr >> 8;
	packetBuffer[6] = addr & 255;
	packetBuffer[7] = packetBuffer[4] ^ packetBuffer[5] ^ packetBuffer[6];

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, 8);
	udp->endPacket();
}

/**
 * Sendet der Z21 einen BroadcastRequest
 */
void CmdReceiverZ21Wlan::enableBroadcasts() {
	//  Serial.println("Sending Broadcast Request");
	memset(packetBuffer, 0, packetBufferSize);

	// 0x08 0x00 0x50 0x00 0x01 0x00 0x01 0x00
	// 2.16 LAN_SET_BROADCASTFLAGS
	packetBuffer[0] = 0x08;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x50;
	packetBuffer[3] = 0x00;

	// Flags = 0x00010001 (Little Endian)
	// 0x01 and 0x010000
	packetBuffer[4] = 0x01;
	packetBuffer[5] = 0x00;
	packetBuffer[6] = 0x01;
	packetBuffer[7] = 0x00;

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, 8);
	udp->endPacket();
}

/**
 * Sendet einen Weichenbefehl, der über das Webinterface ausgelöst wurde, an die Z21
 */
void CmdReceiverZ21Wlan::sendSetTurnout(String id, String status) {
	//Serial.println("Sending Set Turnout");
	//addToLog("Web-Request ID: " + String(id) + " Status: " + status);
	int statuscode = 0;
	if (status == "1") {
		statuscode = 1;
	}
	memset(packetBuffer, 0, packetBufferSize);

	// 5.2 LAN_X_SET_TURNOUT
	packetBuffer[0] = 0x09;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x40;
	packetBuffer[3] = 0x00;

	packetBuffer[4] = 0x53;
	packetBuffer[5] = 0x00;
	packetBuffer[6] = id.toInt();
	packetBuffer[7] = 0xA8 | statuscode;
	packetBuffer[8] = packetBuffer[4] ^ packetBuffer[5] ^ packetBuffer[6]
			^ packetBuffer[7];

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}

CmdReceiverZ21Wlan::~CmdReceiverZ21Wlan() {
}

void CmdReceiverZ21Wlan::resetTimeout() {
	timeout = millis();
}

void CmdReceiverZ21Wlan::sendLanGetSerialNumber() {
	memset(packetBuffer, 0, packetBufferSize);

	// 5.2 LAN_X_SET_TURNOUT
	packetBuffer[0] = 0x04;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x10;
	packetBuffer[3] = 0x00;

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}

void CmdReceiverZ21Wlan::sendCfg12Request() {
	memset(packetBuffer, 0, packetBufferSize);
	// undokumentiert;	04:00:12:00
	packetBuffer[0] = 0x04;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x12;
	packetBuffer[3] = 0x00;

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}

void CmdReceiverZ21Wlan::sendCfg16Request() {
	memset(packetBuffer, 0, packetBufferSize);
	//	undokumentiert; 04:00:16:00
	packetBuffer[0] = 0x04;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x16;
	packetBuffer[3] = 0x00;

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}

void CmdReceiverZ21Wlan::printPacketBuffer(int size) {
	// Print unknown packets:
//	Serial.println("New Paket:");
//	for (int i = 0; i < cb; i++) {
//		Serial.print("&& packetBuffer["+ String(i) + "] == 0x");
//		if (packetBuffer[i] < 16) {
//			Serial.print("0");
//		}
//		Serial.print(packetBuffer[i], HEX);
//		Serial.print(" ");
//	}
	for (int i = 0; i < size; i++) {
		Serial.print(Utils::getHex(packetBuffer[i]));
		Serial.print(" ");
	}
	Serial.println();
}

void CmdReceiverZ21Wlan::sendXGetStatus() {
	memset(packetBuffer, 0, packetBufferSize);

	// 2.4 LAN_X_GET_STATUS
	packetBuffer[0] = 0x07;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x40;
	packetBuffer[3] = 0x00;

	packetBuffer[4] = 0x21;
	packetBuffer[5] = 0x24;
	packetBuffer[6] = 0x05;

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}

void CmdReceiverZ21Wlan::emergencyStop() {
	controller->emergencyStop();
}

void CmdReceiverZ21Wlan::requestLocoInfo(int addr) {
	memset(packetBuffer, 0, packetBufferSize);

	// 4.1 LAN_X_GET_LOCO_INFO
	packetBuffer[0] = 0x09;
	packetBuffer[1] = 0x00;
	packetBuffer[2] = 0x40;
	packetBuffer[3] = 0x00;

	packetBuffer[4] = 0xE3;
	packetBuffer[5] = 0xF0;

	packetBuffer[6] = addr >> 8;
	if (addr >= 128) {
		packetBuffer[6] += 0b11000000;
	}

	packetBuffer[7] = addr & 0xFF;
	packetBuffer[8] = packetBuffer[4] ^ packetBuffer[5] ^ packetBuffer[6]
			^ packetBuffer[7];

	udp->beginPacket(*z21Server, localPort);
	udp->write(packetBuffer, packetBuffer[0]);
	udp->endPacket();
}
