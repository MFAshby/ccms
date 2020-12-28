-- Contains the installed themes
create table if not exists theme (
	id integer not null primary key,
	-- contains the HTML page template for this theme
	-- this will contain placeholder values for content
	-- and navigation
	html text not null
);

-- Contains lanaguage-specific values for themes
create table if not exists theme_content (
	id integer not null primary key,
	theme_id integer not null,
	language text not null,
	-- the key used in the theme's html template
	key text not null,
	-- the value to be written into the template
	value text not null,
	foreign key (theme_id) references theme(id)
);

-- Virtual servers that this actual server is hosting. 
-- Allows running multiple sites from the same server
create table if not exists server (
	id integer not null primary key,
	-- the hostname that is directed to this server, e.g. www.mylovelyhorse.com
	hostname text not null unique,
	-- the language used if the user hasn't selected a different one via the Accept-Language 
	-- http header
	default_language text not null, 
	-- the selected theme for the server
	theme_id int not null, 
	is_default int not null check (is_default in (0,1)),
	foreign key (theme_id) references theme(id)
);

-- Pages that this server is hosting
create table if not exists page (
	id integer not null primary key,
	-- the servet to which this page belongs
	server_id int not null,
	-- for navigation, null indicates a top level page with no parent
	parent_page_id int, 
	-- the path at which this page should be served
	relative_path text not null unique,
	-- if set, indicates that this page has been deprecated
	-- and the user should be redirected to the replacement page
	replacement_page_id int, 
	-- boolean value. If true, this page has been scrubbed and 
	-- the user should be sent a http '410 Gone' response
	purge int,
	-- the last time this page was modified, as a unix timestamp
	last_modified int not null,
	foreign key (server_id) references server(id),
	foreign key (parent_page_id) references page(id),
	foreign key (replacement_page_id) references page(id)
);

-- Language specific page content 
create table if not exists page_content (
	id integer not null primary key,
	-- the page this content should be displayed on
	page_id int not null,
	-- the language used for this content
	language text not null,
	-- the language specific title for the content
	title text not null,
	-- the language specific content itself
	content text not null,
	foreign key (page_id) references page(id)
);

-- Security for the administrative interface
create table if not exists user (
	id integer primary key not null, 
	username text not null,
	password_hash text not null,
	-- for account recovery only
	email_address text not null
);

-- Not entirely sure this should be a database table but hey ho.
-- this is the secret data used to sign JSON Web Tokens for 
-- authentication. It means we don't need to store a session on the 
-- server side.
create table if not exists jwt_secret (
	secret blob
);


-------------------- TESTING DATA --------------------------
delete from theme;
insert into theme(html) values(
	'
	<html>
	<head><title>{{ title }}</title></head>
	<body><p>{{ content }}</p></body>
	</html>
	'
);


delete from server;
insert into server(hostname, default_language, is_default, theme_id) values(
	'localhost:8000',
	'en',
	1,
	(select max(id) from theme)
);

delete from page;
insert into page(server_id, relative_path, last_modified) values(
	(select max(id) from server),
	"/hello",
	strftime('%s', 'now')
);

delete from page_content;
insert into page_content(page_id, language, title, content) values(
	(select max(id) from page),
	'en',
	'hello',
	'you made it this far!'
);
