# ccms
A tiny CMS written in C.
Why? Because:
- I can :) I like a good code-kata sometimes. I might write about this project if I see it through to a reasonable level of completion.
- I've never written a moderately large project in C before.
- I am interested in serving dynamic web content from the smallest possible devices (e.g. ESP-8266). 

Why not a static site generator?
- It's been done many, many times already!
- A good dynamic application with a cache in front can potentially achieve a similar performance.

How?
- See rough design.
- Planning to do the entire thing in GitHub codespaces if I can, no need to shuffle files around between computers.

## Rough, rough design:
I'm planning to just write the glue code. The hard stuff will all be handled by nice libraries that I have found:

### SQLite3 database for storage
Why? Because:
- having all your application data in a single file makes backup easy
- the CMS data model can easily be relational, and a relational DB can enforce this in ways that flat files can't
- it's a single C source file with a well-documented and very well tested API

### Mongoose embedded http server
Why? Because:
- gotta present that content on the web somehow. 
- it's more flexible and faster than CGI
- it's a single C source file with a well-documented API

### Libctemplate for HTML templates
Why? Because: 
- HTML templating is a hard problem
- it's a single C source file with a well-documented API

### Content will be presented as plain HTML & CSS pages
Why? Because: 
- plain HTML & CSS is accessible
- plain HTML & CSS should work OK in most browsers
- Consuming content is mostly a non-interactive activity

### Editor is built in javascript 
Why? Because: 
- Editing content is an interactive activity
- A nice editing experience has features like auto-save, preview etc. 

## Data model
Some ideas taken from WordPress,
- Editing theme and layout and editing content should be totally separate activities. 
- Reasonably pretty default themes should be available.
- Savvy users should be able to create their own themes.
- Navigation should be hierarchical.
- URLs should be relatively intuitive and they should not change. 

Something like this: 
server:
- hostname
- default locale
- theme reference
- certificates?

page:
- server reference
- parent page reference
- relative path (will appear as the element below parent's path)
- replaced by reference (if you edit a page and change the path, we should retain the old path and redirect)
- purge (if you really want to scrub a page, any requests to this path should return http 410. A simple delete will return a 404 instead)
- last_modified (Relevant for cacheing)

page_content:
- page reference
- locale
- title
- text content

theme:
- html, The HTML template for a theme. Must provide navigation. Can provide headers, footers, sidebars etc.

theme_content: 
- theme reference
- locale
- key-value store for texts used in theme.

user: 
- username
- password_hash
- email (for account recovery only)
- 2FA secret?

session: 
- whatever you need for a user session, look it up I guess. 

## Rough work plan & features list
- Setup the database as described in 'Data model'
- Routing. 
- A default theme.
- Templating
- Navigation
- Authentication
- Basic editing
(at this point you have a very simplistic, but usable implementation)
- Check accessibility guidelines.
- Language selector.
- Different language editing.
- Mobile, responsive layout.
- HTTPS 

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
