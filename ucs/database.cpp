/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2008 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/


#include "../common/global_define.h"
#include "../common/eqemu_logsys.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errmsg.h>
#include <mysqld_error.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>
#include <map>

// Disgrace: for windows compile
#ifdef _WINDOWS
#include <windows.h>
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp	_stricmp
#else
#include "../common/unix.h"
#include <netinet/in.h>
#endif

#include "database.h"
#include "../common/eq_packet_structs.h"
#include "../common/misc_functions.h"
#include "../common/string_util.h"
#include "chatchannel.h"

extern Clientlist *g_Clientlist;
extern std::string GetMailPrefix();
extern ChatChannelList *ChannelList;
extern uint32 MailMessagesSent;

Database::Database ()
{
	DBInitVars();
}

/*
Establish a connection to a mysql database with the supplied parameters
*/

Database::Database(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
{
	DBInitVars();
	Connect(host, user, passwd, database, port);
}

bool Database::Connect(const char* host, const char* user, const char* passwd, const char* database, uint32 port)
{
	uint32 errnum= 0;
	char errbuf[MYSQL_ERRMSG_SIZE];
	if (!Open(host, user, passwd, database, port, &errnum, errbuf))
	{
		Log.Out(Logs::General, Logs::Error, "Failed to connect to database: Error: %s", errbuf);
		HandleMysqlError(errnum);

		return false;
	}
	else
	{
		Log.Out(Logs::General, Logs::Status, "Using database '%s' at %s:%d",database,host,port);
		return true;
	}
}

void Database::DBInitVars() {

}



void Database::HandleMysqlError(uint32 errnum) {
}

/*

Close the connection to the database
*/
Database::~Database()
{
}

void Database::GetAccountStatus(Client *client) {

	std::string query = StringFormat("SELECT `status`, `hideme`, `karma`, `revoked` "
                                    "FROM `account` WHERE `id` = '%i' LIMIT 1",
                                    client->GetAccountID());
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		Log.Out(Logs::Detail, Logs::UCS_Server, "Unable to get account status for character %s, error %s", client->GetName().c_str(), results.ErrorMessage().c_str());
		return;
	}

	Log.Out(Logs::Detail, Logs::UCS_Server, "GetAccountStatus Query: %s", query.c_str());

	if(results.RowCount() != 1)
	{
		Log.Out(Logs::Detail, Logs::UCS_Server, "Error in GetAccountStatus");
		return;
	}

	auto row = results.begin();

	client->SetAccountStatus(atoi(row[0]));
	client->SetHideMe(atoi(row[1]) != 0);
	client->SetKarma(atoi(row[2]));
	client->SetRevoked((atoi(row[3])==1?true:false));

	Log.Out(Logs::Detail, Logs::UCS_Server, "Set account status to %i, hideme to %i and karma to %i for %s", client->GetAccountStatus(), client->GetHideMe(), client->GetKarma(), client->GetName().c_str());

}

int Database::FindAccount(const char *characterName, Client *client) {

	Log.Out(Logs::Detail, Logs::UCS_Server, "FindAccount for character %s", characterName);


	client->ClearCharacters();
    std::string query = StringFormat("SELECT `id`, `account_id`, `level` "
                                    "FROM `character_data` WHERE `name` = '%s' LIMIT 1",
                                    characterName);
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::Detail, Logs::UCS_Server, "FindAccount query failed: %s", query.c_str());
		return -1;
	}

	if (results.RowCount() != 1) {
		Log.Out(Logs::Detail, Logs::UCS_Server, "Bad result from query");
		return -1;
	}

	auto row = results.begin();
	client->AddCharacter(atoi(row[0]), characterName, atoi(row[2]));

	int accountID = atoi(row[1]);

	Log.Out(Logs::Detail, Logs::UCS_Server, "Account ID for %s is %i", characterName, accountID);

    query = StringFormat("SELECT `id`, `name`, `level` FROM `character_data` "
                        "WHERE `account_id` = %i AND `name` != '%s'",
						accountID, characterName);
    results = QueryDatabase(query);
	if (!results.Success())
		return accountID;

	for (auto row = results.begin(); row != results.end(); ++row)
		client->AddCharacter(atoi(row[0]), row[1], atoi(row[2]));

	return accountID;
}

bool Database::VerifyMailKey(std::string characterName, int IPAddress, std::string MailKey) {

	std::string query = StringFormat("SELECT `mailkey` FROM `character_data` WHERE `name`='%s' LIMIT 1",
                                    characterName.c_str());
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		Log.Out(Logs::Detail, Logs::UCS_Server, "Error retrieving mailkey from database: %s", results.ErrorMessage().c_str());
		return false;
	}

	auto row = results.begin();

	// The key is the client's IP address (expressed as 8 hex digits) and an 8 hex digit random string generated
	// by world.
	//
	char combinedKey[17];

	if(RuleB(Chat, EnableMailKeyIPVerification) == true)
		sprintf(combinedKey, "%08X%s", IPAddress, MailKey.c_str());
	else
		sprintf(combinedKey, "%s", MailKey.c_str());

	Log.Out(Logs::Detail, Logs::UCS_Server, "DB key is [%s], Client key is [%s]", (row[0] ? row[0] : ""), combinedKey);

	return !strcmp(row[0], combinedKey);
}

int Database::FindCharacter(const char *characterName)
{
	char *safeCharName = RemoveApostrophes(characterName);
	std::string query = StringFormat("SELECT `id` FROM `character_data` WHERE `name`='%s' LIMIT 1", safeCharName);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
		safe_delete_array(safeCharName);
		return -1;
	}

	safe_delete_array(safeCharName);

	if (results.RowCount() != 1) {
		Log.Out(Logs::Detail, Logs::UCS_Server, "Bad result from FindCharacter query for character %s",
			characterName);
		return -1;
	}

	auto row = results.begin();

	int characterID = atoi(row[0]);

	return characterID;
}

bool Database::GetVariable(const char* varname, char* varvalue, uint16 varvalue_len) {

	std::string query = StringFormat("SELECT `value` FROM `variables` WHERE `varname` = '%s'", varname);
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}

	if (results.RowCount() != 1)
		return false;

	auto row = results.begin();

	snprintf(varvalue, varvalue_len, "%s", row[0]);

	return true;
}

bool Database::LoadChatChannels() {

	Log.Out(Logs::Detail, Logs::UCS_Server, "Loading chat channels from the database.");

	const std::string query = "SELECT `name`, `owner`, `password`, `minstatus` FROM `chatchannels`";
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		return false;
	}

	for (auto row = results.begin();row != results.end(); ++row) {
		std::string channelName = row[0];
		std::string channelOwner = row[1];
		std::string channelPassword = row[2];

		ChannelList->CreateChannel(channelName, channelOwner, channelPassword, true, atoi(row[3]));
	}

	return true;
}

void Database::SetChannelPassword(std::string channelName, std::string password) {

	Log.Out(Logs::Detail, Logs::UCS_Server, "Database::SetChannelPassword(%s, %s)", channelName.c_str(), password.c_str());

	std::string query = StringFormat("UPDATE `chatchannels` SET `password` = '%s' WHERE `name` = '%s'",
                                    password.c_str(), channelName.c_str());
    QueryDatabase(query);
}

void Database::SetChannelOwner(std::string channelName, std::string owner) {

	Log.Out(Logs::Detail, Logs::UCS_Server, "Database::SetChannelOwner(%s, %s)", channelName.c_str(), owner.c_str());

	std::string query = StringFormat("UPDATE `chatchannels` SET `owner` = '%s' WHERE `name` = '%s'",
                                    owner.c_str(), channelName.c_str());
    QueryDatabase(query);
}

void Database::SendHeaders(Client *client) {

	int unknownField2 = 25015275;
	int unknownField3 = 1;
	int characterID = FindCharacter(client->MailBoxName().c_str());

	Log.Out(Logs::Detail, Logs::UCS_Server, "Sendheaders for %s, CharID is %i", client->MailBoxName().c_str(), characterID);

	if(characterID <= 0)
		return;

	std::string query = StringFormat("SELECT `msgid`,`timestamp`, `from`, `subject`, `status` "
                                    "FROM `mail` WHERE `charid`=%i", characterID);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return;

	char buffer[100];

	int headerCountPacketLength = 0;

	sprintf(buffer, "%i", client->GetMailBoxNumber());
	headerCountPacketLength += (strlen(buffer) + 1);

	sprintf(buffer, "%i", unknownField2);
	headerCountPacketLength += (strlen(buffer) + 1);

	sprintf(buffer, "%i", unknownField3);
	headerCountPacketLength += (strlen(buffer) + 1);

	sprintf(buffer, "%i", results.RowCount());
	headerCountPacketLength += (strlen(buffer) + 1);

	auto outapp = new EQApplicationPacket(OP_MailHeaderCount, headerCountPacketLength);

	char *packetBuffer = (char *)outapp->pBuffer;

	VARSTRUCT_ENCODE_INTSTRING(packetBuffer, client->GetMailBoxNumber());
	VARSTRUCT_ENCODE_INTSTRING(packetBuffer, unknownField2);
	VARSTRUCT_ENCODE_INTSTRING(packetBuffer, unknownField3);
	VARSTRUCT_ENCODE_INTSTRING(packetBuffer, results.RowCount());


	client->QueuePacket(outapp);

	safe_delete(outapp);

	int rowIndex = 0;
	for(auto row = results.begin(); row != results.end(); ++row, ++rowIndex) {
		int headerPacketLength = 0;

		sprintf(buffer, "%i", client->GetMailBoxNumber());
		headerPacketLength += strlen(buffer) + 1;
		sprintf(buffer, "%i", unknownField2);
		headerPacketLength += strlen(buffer) + 1;
		sprintf(buffer, "%i", rowIndex);
		headerPacketLength += strlen(buffer) + 1;

		headerPacketLength += strlen(row[0]) + 1;
		headerPacketLength += strlen(row[1]) + 1;
		headerPacketLength += strlen(row[4]) + 1;
		headerPacketLength += GetMailPrefix().length() + strlen(row[2]) + 1;
		headerPacketLength += strlen(row[3]) + 1;

		outapp = new EQApplicationPacket(OP_MailHeader, headerPacketLength);

		packetBuffer = (char *)outapp->pBuffer;

		VARSTRUCT_ENCODE_INTSTRING(packetBuffer, client->GetMailBoxNumber());
		VARSTRUCT_ENCODE_INTSTRING(packetBuffer, unknownField2);
		VARSTRUCT_ENCODE_INTSTRING(packetBuffer, rowIndex);
		VARSTRUCT_ENCODE_STRING(packetBuffer, row[0]);
		VARSTRUCT_ENCODE_STRING(packetBuffer, row[1]);
		VARSTRUCT_ENCODE_STRING(packetBuffer, row[4]);
		VARSTRUCT_ENCODE_STRING(packetBuffer, GetMailPrefix().c_str());
		packetBuffer--;
		VARSTRUCT_ENCODE_STRING(packetBuffer, row[2]);
		VARSTRUCT_ENCODE_STRING(packetBuffer, row[3]);


		client->QueuePacket(outapp);

		safe_delete(outapp);
	}

}

void Database::SendBody(Client *client, int messageNumber) {

	int characterID = FindCharacter(client->MailBoxName().c_str());

	Log.Out(Logs::Detail, Logs::UCS_Server, "SendBody: MsgID %i, to %s, CharID is %i", messageNumber, client->MailBoxName().c_str(), characterID);

	if(characterID <= 0)
		return;

	std::string query = StringFormat("SELECT `msgid`, `body`, `to` FROM `mail` "
                                    "WHERE `charid`=%i AND `msgid`=%i", characterID, messageNumber);
    auto results = QueryDatabase(query);
	if (!results.Success())
		return;


	if (results.RowCount() != 1)
		return;

	auto row = results.begin();

	Log.Out(Logs::Detail, Logs::UCS_Server, "Message: %i  body (%i bytes)", messageNumber, strlen(row[1]));

	int packetLength = 12 + strlen(row[0]) + strlen(row[1]) + strlen(row[2]);

	auto outapp = new EQApplicationPacket(OP_MailSendBody, packetLength);

	char *packetBuffer = (char *)outapp->pBuffer;

	VARSTRUCT_ENCODE_INTSTRING(packetBuffer, client->GetMailBoxNumber());
	VARSTRUCT_ENCODE_STRING(packetBuffer,row[0]);
	VARSTRUCT_ENCODE_STRING(packetBuffer,row[1]);
	VARSTRUCT_ENCODE_STRING(packetBuffer,"1");
	VARSTRUCT_ENCODE_TYPE(uint8, packetBuffer, 0);
	VARSTRUCT_ENCODE_TYPE(uint8, packetBuffer, 0x0a);
	VARSTRUCT_ENCODE_STRING(packetBuffer, "TO:");
	packetBuffer--;
	VARSTRUCT_ENCODE_STRING(packetBuffer, row[2]);
	packetBuffer--; // Overwrite the null terminator
	VARSTRUCT_ENCODE_TYPE(uint8, packetBuffer, 0x0a);


	client->QueuePacket(outapp);

	safe_delete(outapp);
}

bool Database::SendMail(std::string recipient, std::string from, std::string subject, std::string body, std::string recipientsString) {

	int characterID;
	std::string characterName;

	auto lastPeriod = recipient.find_last_of(".");

	if(lastPeriod == std::string::npos)
		characterName = recipient;
	else
		characterName = recipient.substr(lastPeriod+1);

	characterName[0] = toupper(characterName[0]);

	for(unsigned int i = 1; i < characterName.length(); i++)
		characterName[i] = tolower(characterName[i]);

	characterID = FindCharacter(characterName.c_str());

	Log.Out(Logs::Detail, Logs::UCS_Server, "SendMail: CharacterID for recipient %s is %i", characterName.c_str(), characterID);

	if(characterID <= 0)
        return false;

	auto escSubject = new char[subject.length() * 2 + 1];
	auto escBody = new char[body.length() * 2 + 1];

	DoEscapeString(escSubject, subject.c_str(), subject.length());
	DoEscapeString(escBody, body.c_str(), body.length());

	int now = time(nullptr); // time returns a 64 bit int on Windows at least, which vsnprintf doesn't like.

    std::string query = StringFormat("INSERT INTO `mail` "
                                    "(`charid`, `timestamp`, `from`, `subject`, `body`, `to`, `status`) "
                                    "VALUES ('%i', %i, '%s', '%s', '%s', '%s', %i)",
                                    characterID, now, from.c_str(), escSubject, escBody,
                                    recipientsString.c_str(), 1);
    safe_delete_array(escSubject);
    safe_delete_array(escBody);
    auto results = QueryDatabase(query);
	if(!results.Success()) {
		return false;
	}

	Log.Out(Logs::Detail, Logs::UCS_Server, "MessageID %i generated, from %s, to %s", results.LastInsertedID(), from.c_str(), recipient.c_str());


	Client *client = g_Clientlist->IsCharacterOnline(characterName);

	if(client) {
		std::string FQN = GetMailPrefix() + from;
		client->SendNotification(client->GetMailBoxNumber(characterName), subject, FQN, results.LastInsertedID());
	}

	MailMessagesSent++;

	return true;
}

void Database::SetMessageStatus(int messageNumber, int status) {

	Log.Out(Logs::Detail, Logs::UCS_Server, "SetMessageStatus %i %i", messageNumber, status);

	if(status == 0) {
        std::string query = StringFormat("DELETE FROM `mail` WHERE `msgid` = %i", messageNumber);
		auto results = QueryDatabase(query);
        return;
    }

    std::string query = StringFormat("UPDATE `mail` SET `status` = %i WHERE `msgid`=%i", status, messageNumber);
    QueryDatabase(query);
}

void Database::ExpireMail() {

	Log.Out(Logs::Detail, Logs::UCS_Server, "Expiring mail...");

	std::string query = "SELECT COUNT(*) FROM `mail`";
    auto results = QueryDatabase(query);
    if (!results.Success()) {
		return;
	}

	auto row = results.begin();

	Log.Out(Logs::Detail, Logs::UCS_Server, "There are %s messages in the database.", row[0]);

	// Expire Trash
	if(RuleI(Mail, ExpireTrash) >= 0) {
        query = StringFormat("DELETE FROM `mail` WHERE `status`=4 AND `timestamp` < %i",
                            time(nullptr) - RuleI(Mail, ExpireTrash));
        results = QueryDatabase(query);
		if(results.Success())
            Log.Out(Logs::Detail, Logs::UCS_Server, "Expired %i trash messages.", results.RowsAffected());
	}

	// Expire Read
	if(RuleI(Mail, ExpireRead) >= 0) {
        query = StringFormat("DELETE FROM `mail` WHERE `status` = 3 AND `timestamp` < %i",
                            time(nullptr) - RuleI(Mail, ExpireRead));
        results = QueryDatabase(query);
		if(results.Success())
            Log.Out(Logs::Detail, Logs::UCS_Server, "Expired %i read messages.", results.RowsAffected());
	}

	// Expire Unread
	if(RuleI(Mail, ExpireUnread) >= 0) {
        query = StringFormat("DELETE FROM `mail` WHERE `status`=1 AND `timestamp` < %i",
                            time(nullptr) - RuleI(Mail, ExpireUnread));
        results = QueryDatabase(query);
		if(results.Success())
            Log.Out(Logs::Detail, Logs::UCS_Server, "Expired %i unread messages.", results.RowsAffected());
	}
}

void Database::AddFriendOrIgnore(int charID, int type, std::string name) {

    std::string query = StringFormat("INSERT INTO `friends` (`charid`, `type`, `name`) "
                                    "VALUES('%i', %i, '%s')",
                                    charID, type, CapitaliseName(name).c_str());
    auto results = QueryDatabase(query);
	if(results.Success())
		Log.Out(Logs::Detail, Logs::UCS_Server, "Wrote Friend/Ignore entry for charid %i, type %i, name %s to database.", charID, type, name.c_str());

}

void Database::RemoveFriendOrIgnore(int charID, int type, std::string name) {

	std::string query = StringFormat("DELETE FROM `friends` WHERE `charid` = %i "
                                    "AND `type` = %i AND `name` = '%s'",
                                    charID, type, CapitaliseName(name).c_str());
    auto results = QueryDatabase(query);
	if(!results.Success())
		Log.Out(Logs::Detail, Logs::UCS_Server, "Error removing friend/ignore, query was %s", query.c_str());
	else
		Log.Out(Logs::Detail, Logs::UCS_Server, "Removed Friend/Ignore entry for charid %i, type %i, name %s from database.", charID, type, name.c_str());

}

void Database::GetFriendsAndIgnore(int charID, std::vector<std::string> &friends, std::vector<std::string> &ignorees) {

	std::string query = StringFormat("select `type`, `name` FROM `friends` WHERE `charid`=%i", charID);
    auto results = QueryDatabase(query);
	if (!results.Success()) {
		return;
	}


	for (auto row = results.begin(); row != results.end(); ++row) {
		std::string name = row[1];

		if(atoi(row[0]) == 0)
		{
			ignorees.push_back(name);
			Log.Out(Logs::Detail, Logs::UCS_Server, "Added Ignoree from DB %s", name.c_str());
			continue;
		}

        friends.push_back(name);
        Log.Out(Logs::Detail, Logs::UCS_Server, "Added Friend from DB %s", name.c_str());
	}

}

void Database::LoadLogSettings(EQEmuLogSys::LogSettings* log_settings){
	std::string query =
		"SELECT "
		"log_category_id, "
		"log_category_description, "
		"log_to_console, "
		"log_to_file, "
		"log_to_gmsay "
		"FROM "
		"logsys_categories "
		"ORDER BY log_category_id";
	auto results = QueryDatabase(query);

	int log_category = 0;
	Log.file_logs_enabled = false;

	for (auto row = results.begin(); row != results.end(); ++row) {
		log_category = atoi(row[0]);
		log_settings[log_category].log_to_console = atoi(row[2]);
		log_settings[log_category].log_to_file = atoi(row[3]);
		log_settings[log_category].log_to_gmsay = atoi(row[4]);

		/* Determine if any output method is enabled for the category
			and set it to 1 so it can used to check if category is enabled */
		const bool log_to_console = log_settings[log_category].log_to_console > 0;
		const bool log_to_file = log_settings[log_category].log_to_file > 0;
		const bool log_to_gmsay = log_settings[log_category].log_to_gmsay > 0;
		const bool is_category_enabled = log_to_console || log_to_file || log_to_gmsay;

		if (is_category_enabled)
			log_settings[log_category].is_category_enabled = 1;

		/*
		This determines whether or not the process needs to actually file log anything.
		If we go through this whole loop and nothing is set to any debug level, there is no point to create a file or keep anything open
		*/
		if (log_settings[log_category].log_to_file > 0){
			Log.file_logs_enabled = true;
		}
	}
}
