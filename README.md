# ccms
A tiny CMS written in C.
Why? Because:
- I can :) 
- I like a good code-kata sometimes. I might write about this project if I see it through to a reasonable level of completion.
- I've never written a moderately large project in C before.
- I am interested in serving dynamic web content from the smallest possible devices (e.g. ESP-8266). 

Why not a static site generator?
- It's been done many, many times already!
- lots of popular software _isn't_ done as static sites, e.g. WordPress. 

# instructions
Dependencies: cmark, json-c, libmicrohttpd
To build:
```bash
make
# to make it faster, use more threads, if you have them
make -j 4
```

To install:
```bash
sudo make install
# to uninstall,
sudo make uninstall
```

To run:
```bash
ccms
# TODO more instructions for configuration...
```


How?
- See rough design.
- Planning to do the entire thing in GitHub codespaces if I can, no need to shuffle files around between computers.

## Rough, rough design:
I'm planning to just write the glue code. The hard stuff will all be handled by nice libraries that I have found:

### Content will be presented as HTML & CSS pages
Why? Because: 
- plain HTML & CSS is accessible
- plain HTML & CSS should work OK in most browsers
- Consuming content is mostly a non-interactive activity

### Editor is built in javascript 
Why? Because: 
- Editing content is an interactive activity
- A nice editing experience has features like auto-save, preview etc. 
- Javascript gives you access to lots of tools to make this happen, e.g. 
-- Websocket for bi directional streaming communication with the server, useful for stuff like collaborative editing
-- fetch api for API calls
-- Interactivity generally :) 

Going to structure the editor as a single page application, I think this makes sense for what is essentially an app and not a page.
- login, gets a token, uses this for API calls to edit content
- maybe use websocket for API calls actually. Advantage is bidi with the server.. 
- so.. suggestion: 
-- slurp the whole server state on websocket init
-- use an update hook to send updates from the server
-- stream updates from the client.
-- OK let's try it.

## Data model
Some ideas taken from WordPress,
- Editing theme and layout and editing content should be totally separate activities. 
- Reasonably pretty default themes should be available.
- Savvy users should be able to create their own themes.
- Navigation should be hierarchical.
- URLs should be relatively intuitive and they should not change. 

## Rough work plan & features list
- Setup the database as described in 'Data model'
- Routing. 
- A default theme.
- Templating
- Navigation
- Basic editing
(at this point you have a very simplistic, but usable implementation, if you ban the editor on your reverse proxy)
- Authentication.
- Check accessibility guidelines.
- Language selector.
- Different language editing.
- Mobile, responsive layout.
- HTTPS
- Authorization.
- Drafts.
- Basic metrics.
- Built-in backups.

## Things that will require consideration
If I was to build this out into a full-features CMS that you could use in the wild...
- Images. Sometimes you want to include images in content
- Links.
- Accessibility. People who aren't you want to read websites sometimes. Let them.
- Translation. Similar to the above, support translated versions of pages for multilingual sites. Also make sure the backend can handle.
- Mobile, adaptive. Make sure your default theme works ok on phones.
- Authentication and Authorization. Some users should be able to make edits, some should not.
- Cacheing, probably need to implement the right headers for things like ETag and Content-Age etc. 
- HTTPS
- Auth session expiry

And optional extras and niceties
- Downloads
- Comments
- Feed readers (rss, atom)
- Sitemap
- Email subscription
- 2FA
- ACME (automatic TLS)

Editor considerations
=====================

Plan is to build the editor as a SPA using vanilla javascript.

This means the editor needs an API to interact with, so something like...

* all API routes under the /api/ path.
* /login swaps username and password for a signed token, do with it what you will. 
* simple CRUD API that basically follows the database schema?
** GET /server gets all servers
** POST /server adds a new server with specified parameters
** GET /page gets all pages
** POST /page adds a new page with specified parameters
** PUT /page/<page_id> edits a page parameters. 
** GET /page_content lists all page contents
** POST /page_content posts a new page content with specified parameters
** PUT /page_content/<page_content_id> edits a page_content parameters.
