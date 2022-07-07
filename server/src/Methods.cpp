#include "Client.hpp"
#include "Utils.hpp"
#include <errno.h>

void Client::makeGetResponse( void )
{
	if (status & HEAD_SENT) {
		if (res.sendResponse_file(socket))
			status |= RESP_DONE;
	}
	else {
		if (res.sendResponse_stream(socket)) {
			if (status & REDIRECT)
				status |= RESP_DONE;
			else
				status |= HEAD_SENT;
		}
	}
	if (status & RESP_DONE)
		cleaner();
}

void Client::makePostResponse( void )
{
	char				buf[BUF];
	int					wr = 0;
	int					rd = 0;
	long				bytesRead = 0;

	if (reader_size == 0 && !(status & CGI_DONE)) {
		res.make_response_html(201, resCode[201]);
		status |= CGI_DONE;
	}
	if (!(status & CGI_DONE))
	{
		if (status & IS_WRITE) {
			bzero(buf, BUF);
			reader.read(buf, BUF);
			bytesRead = reader.gcount();
			wr = write(res.getPipeWrite(), buf, bytesRead);
			if (wr > 0) {
				wrtRet += wr;
				countw +=wr;
			}
			if (wr < bytesRead)
				reader.seekg(wrtRet);
			if (countw >= BUF || wr == -1 || wrtRet == reader_size) {
				countw = 0;
				status &= ~IS_WRITE;
			}
			if (wrtRet >= reader_size)
				close(res.getPipeWrite());
		}
		else{
			bzero(buf, BUF);
			rd = read(res.getPipeRead(), buf, BUF);
			if (rd > 0) {
				rdRet += rd;
				countr += rd;
			}
			if ((countr >= BUF || rd == -1)) {
				countr = 0;
				status |= IS_WRITE;
			}
			res.getStrStream() << buf;
		}
		if (rdRet >= wrtRet && wrtRet == reader_size) //SIGPIPE
		{
			clearStream();
			
			close(res.getPipeRead());
			statusCode = res.extractCgiHeader(req);
			std::stringstream tmp;
			tmp << res.getStrStream().rdbuf();
			clearStrStream(res.getStrStream());
			statusCode = 201;
			res.make_response_header(req, statusCode, resCode[statusCode], getStrStreamSize(tmp));
			res.getStrStream() << tmp.rdbuf();
			status |= CGI_DONE;
		}
	}
	else if (status & CGI_DONE)	//если все данные передались в cgi
	{
		if (res.sendResponse_stream(socket)) {
			status |= RESP_DONE;
		}
	}
	if (status & RESP_DONE)
		cleaner();
}

void Client:: makeDeleteResponse( void )	{
	if (remove(location.c_str()) != 0) {
		debug_msg(1, RED, "Can't remove file: Permisson denied: ", location);
		throw codeException(403);
	}
	else {
		statusCode = 204;
		res.setFileLoc(location);
		clearStrStream(res.getStrStream());
		res.make_response_html(statusCode, resCode[statusCode]);
		if (res.sendResponse_stream(socket))  {
			status |= RESP_DONE;
			cleaner();
		}
	}
}

void Client:: makePutResponse( void )	{
	std::ofstream file(location);
	if (!file.is_open()) {
		size_t sep = location.find_last_of("/");
		if (sep != std::string::npos) {
			rek_mkdir(location.substr(0, sep));
		}
		file.open(location);
	}
	if (file.is_open()) {
		file << reader.str();
		file.close();
	} else {
		debug_msg(1, RED, "File is not open: ", location);
		throw codeException(406);
	}
	statusCode = 201;
	res.setFileLoc(location);
	clearStrStream(res.getStrStream());
	res.make_response_html(201, resCode[201]);
	if (res.sendResponse_stream(socket))  {
		status |= RESP_DONE;
		cleaner();
	}
}