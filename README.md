# cometpsd

Simple comet pub/sub server with an optional shared secret for controlling publish rights.

Open-source licensed under MIT, by [Rasmus Andersson](http://hunch.se/).


## Example

Start a server in a terminal:

	$ cometpsd -k xyz

Open [http://localhost:8080/channel](http://localhost:8080/channel) in your web browser.

In another terminal, use curl to publish something:

	$ curl -i -X POST -H 'X-CPS-Publish-Key: xyz' \
	  --data-binary '<p>hello</p>' localhost:8080/channel

"hello" should appear in your browser when you run the command above. Open more tabs with  [http://localhost:8080/channel](http://localhost:8080/channel) and run the curl command a few more times.
