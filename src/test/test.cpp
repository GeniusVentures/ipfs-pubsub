#include <iostream>
#include <sstream>

#include <ipfs/client.h>
#include <curl/curl.h>


int main(int, char**) {
   
	CURL * curl;
	curl_global_init(CURL_GLOBAL_DEFAULT);
	std::stringstream contents;

	ipfs::Client client("localhost", 5001);

	client.FilesGet("/ipfs/QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG/readme", &contents);

	std::cout << contents.str() << std::endl;

	return 0;

  return 0;
}