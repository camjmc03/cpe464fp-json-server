#!/bin/sh

echo -n -e "Content-Type: text/html\r\n"
echo -n -e "\r\n"
echo -n -e "<HTML><HEAD><TITLE>CGI Script Parameter Table</TITLE></HEAD><BODY>\r\n"
echo -n -e "<TABLE BORDER=\"1\"><TR><TH>Parameter Name</TH><TH>Value</TH></TR>\r\n"
echo -n -e "<TR><TD>GATEWAY_INTERFACE</TD><TD>$GATEWAY_INTERFACE</TD></TR>\r\n"
echo -n -e "<TR><TD>QUERY_STRING</TD><TD>$QUERY_STRING</TD></TR>\r\n"
echo -n -e "<TR><TD>REMOTE_ADDR</TD><TD>$REMOTE_ADDR</TD></TR>\r\n"
echo -n -e "<TR><TD>REQUEST_METHOD</TD><TD>$REQUEST_METHOD</TD></TR>\r\n"
echo -n -e "<TR><TD>SCRIPT_NAME</TD><TD>$SCRIPT_NAME</TD></TR>\r\n"
echo -n -e "<TR><TD>SERVER_NAME</TD><TD>$SERVER_NAME</TD></TR>\r\n"
echo -n -e "<TR><TD>SERVER_PORT</TD><TD>$SERVER_PORT</TD></TR>\r\n"
echo -n -e "<TR><TD>SERVER_PROTOCOL</TD><TD>$SERVER_PROTOCOL</TD></TR>\r\n"
echo -n -e "</TABLE></BODY></HTML>\r\n"