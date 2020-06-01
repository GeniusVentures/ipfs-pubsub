#include <iostream>
#include <sstream>

#include <ipfs/client.h>


int main(int, char**) {
   
	std::stringstream contents;

	ipfs::Client client("localhost", 5001);

	client.FilesGet("/ipfs/QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG/readme", &contents);

	std::cout << contents.str() << std::endl;

	return 0;

  return 0;
}