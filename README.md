# cometpsd

Simple comet pub/sub server with an optional shared secret for controlling publish rights.

Open source licensed under MIT, by [Rasmus Andersson](http://hunch.se/).


## Example

Start a server in a terminal:

	$ cometpsd -k xyz

Open [http://localhost:8080/channel/default](http://localhost:8080/channel/default) in your web browser.

In another terminal, use curl to publish something:

	$ curl -i -X POST -H 'X-CPS-Publish-Key: xyz' \
	  --data-binary '<p>hello</p>' localhost:8080/channel/default

"hello" should appear in your browser when you run the command above. Open more tabs with  [http://localhost:8080/channel/default](http://localhost:8080/channel/default) 
and run the curl command a few more times.


## Configuration file

The server can be configured by a YAML file (passing filename, or "-" for stdin, with `-f` flag)
which can describe multiple servers, which in turn each can describe multiple channels.

Parameters like `address`, `port` and `log_level` are hierarchical
-- i.e. specifying `log_level 3` for a server will implicitly set `log_level 3` for all 
its channels which do not themselves set `log_level`.

### Example

	servers:
	  - address: "0.0.0.0" # ANY
	    port: 8080
	    channels:
	      test:
	        publish_key: xyz
	        log_level: 3
	      test2:
	        max_clients: 3
  
	  - port: 8081
	    log_level: 2
	    channels: {a: {publish_key: xyz}, b: {}}

