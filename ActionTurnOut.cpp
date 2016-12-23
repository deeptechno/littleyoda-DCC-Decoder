/*
 * ActionTurnOut.cpp
 *
 *  Created on: 19.08.2016
 *      Author: sven
 */

#include "Arduino.h"
#include "ActionTurnOut.h"
#include "Logger.h"

ActionTurnOut::ActionTurnOut(int dir1, int dir2, int enable, int id) {
	Logger::getInstance()->addToLog(
			"Starting Turnout Dir " + String(dir1) + "/" + String(dir2) + " Enable: "
					+ enable + " ID: " + id);
	dirPin[0] = dir1;
	dirPin[1] = dir2;
	this->enable = enable;
	this->id = id;
	int i;
	for (i = 0; i < 2; i++) {
		pinMode(dirPin[i], OUTPUT);
		digitalWrite(dirPin[i], 0);
	}
	pinMode(enable, OUTPUT);
	off();

}

String ActionTurnOut::getHTMLCfg(String urlprefix) {
	return "";
}

String ActionTurnOut::getHTMLController(String urlprefix) {
	String message =  "<div class=\"row\"> <div class=\"column column-10\">";
	message += "Turnout-ID ";
	message += String(id);
	message += "</div>";
	message += "<div class=\"column column-90\"><a style=\"font-size: 4rem;\" class=\"button\" href=\"";
	message += urlprefix;
	message += "value=0";
	message += "\">&#9756;</a>";
	message += " <a style=\"font-size: 4rem;\" class=\"button\" href=\"";
	message += urlprefix;
	message += "value=1";
	message += "\">&#9758;</a></div>";
	message += "</div>";
	return message;
}

void ActionTurnOut::off() {
	digitalWrite(enable, 0);
	for (int i = 0; i < 2; i++) {
		digitalWrite(dirPin[i], 0);
	}
}

int ActionTurnOut::loop() {
	off();
	return 1000;
}

void ActionTurnOut::TurnoutCmd(int id, int status, int source) {
	if (id != this->id) {
		return;
	}
	if (status == 1) {
		digitalWrite(dirPin[0], 0);
		digitalWrite(dirPin[1], 1);
	} else {
		digitalWrite(dirPin[0], 1);
		digitalWrite(dirPin[1], 0);
	}
	digitalWrite(enable, 1);
	delay(200);
	off();
}

ActionTurnOut::~ActionTurnOut() {
}

void ActionTurnOut::setSettings(String key, String value) {
	TurnoutCmd(id, value.toInt(), -1);
}
