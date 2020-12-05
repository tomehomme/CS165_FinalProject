Paris Hom 862062330

Brandon Cai 862080765

CS165

Fall 2020

#
## CS165 Final Project Readme

Paris was in charge of selecting proxies using rendezvous hashing in the client.

Paris was also in charge of the bloom filter implementation. This included reading the forbidden objects from a file and mapping each file to their respective proxy. As well as identifying forbidden objects using the bloom filter and logic for when to check the cache.

Paris was also in charge of implementing the proxy cache and multiple (5) proxies.

Brandon was in charge of TLS implementation. This included creating the TLS sockets and connections as well as verifying TLS handshakes between clients, proxy, and server. He was also responsible for file transfers between entities through TLS.

**To run the project,**

1. Have file &#39;blacklisted.txt&#39; in &#39;proxy&#39; folder and &#39;files.txt&#39; in &#39;server&#39; folder
2. In the &#39;build/src&#39; folder, start up the three parties
  1. Start up the server &#39;./server&#39;
  2. Start up proxies &#39;./proxy&#39;
  3. Start up client &#39;./client \&lt;fileName\&gt;
3. You should be able to see &#39;fileName: content&#39; in the client side if the request was accepted
  1. If the request was denied (blacklisted or not available), you should see &#39;Access Denied&#39;

**Example file names to test:**

**Valid files** : text1.txt ; skeleton.txt ; witcher.txt ; catherine.txt

**Blacklisted file** : blacklistedfile.txt

**Example command line** : (within /build/) use

**./src/client text1.txt**

**To test cache:**

1. Clients can request the same file twice in a row. On the second run you should see a message indicating that the file was found in the cache. This is because whenever a file is not found in cache, it is requested from the server and then stored into a local cache within the proxy.

**Notes** : server uses port 9998, proxies use port 9990-9995, we assume that the client will decide which port to connect to. This is so that the user does not choose a server port that is already being used by the 5 proxies.

**References:**

[https://github.com/bob-beck/libtls/blob/master/TUTORIAL.md](https://github.com/bob-beck/libtls/blob/master/TUTORIAL.md)

[https://man.openbsd.org/tls\_init.3](https://man.openbsd.org/tls_init.3)