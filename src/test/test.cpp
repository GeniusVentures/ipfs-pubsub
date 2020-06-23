#include <iostream>
#include <sstream>


#include "../pubsubroom.hpp"

int main(int, char**) {
   
	// std::stringstream contents;

	// ipfs::Client client("localhost", 5001);

	// client.FilesGet("/ipfs/QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG/readme", &contents);

	// std::cout << contents.str() << std::endl;
	pubsubroom::PubsubRoom pubsubroom1("room1", 5002);
	std::string testMsg =  "--Testing Pubsubroom...--";
	std:cout << testMsg << std::endl;
	return 0;
}