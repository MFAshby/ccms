-- Contains the installed themes
create table if not exists theme (
	id integer not null primary key,
	-- Contains the template for this theme
	template text not null
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
	is_default int not null default 0 check (is_default in (0,1)),
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
	relative_path text not null,
	-- if set, indicates that this page has been deprecated
	-- and the user should be redirected to the replacement page
	replacement_page_id int, 
	-- boolean value. If true, this page has been scrubbed and 
	-- the user should be sent a http '410 Gone' response
	purge int,
	-- the last time this page was modified, as a unix timestamp
	last_modified int not null default current_timestamp,
	foreign key (server_id) references server(id),
	foreign key (parent_page_id) references page(id),
	foreign key (replacement_page_id) references page(id)
);
create unique index if not exists page_server_id_relative_path_idx on page (
	server_id, 
	relative_path
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

create unique index if not exists page_content_page_id_language_idx on page_content (
	page_id, 
	language	
);

-- Static resources, a simple KV store
create table if not exists static_resources (
	id integer primary key not null,
	key text not null,
	server_id int not null,
	value blob not null,
	content_type text not null,
	foreign key (server_id) references server(id)
);
create unique index if not exists static_resources_server_id_key on static_resources (
	server_id, 
	key
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
insert or ignore into theme(id, template) values(
	1,
	'<!DOCTYPE html>
	<html lang="{{ language }}">
	<head>
		<title>{{ title }}</title>
		<meta charset="UTF-8"/> 
		<link rel="stylesheet" href="static/main.css"/>
	</head>
	<body>
		<h4>{{ blogname }}</h4>
		<nav>
			<ol>
			{{#nav}}
			<li><a href="{{ url }}">{{ title }}</a></li>
			{{/nav}}
			</ol>
		</nav>
		<h2>{{ title }}</h2>
		<!-- content is pre-rendered with cmark, so HTML should be allowed -->
		<p>{{{ content }}}</p>
		<p>{{ tagline }}</p>
	</body>
	</html>'
);

insert or ignore into theme_content(id, theme_id, language, key, value) values (
	1,
	1, 
	'en',
	'tagline',
	'Served with ccms https://fossil.mfashby.net/dir?ci=tip&name=ccms'
), (
	2,
	1,
	'en',
	'blogname',
	'mfashby.net'
);

insert or ignore into server(id, hostname, default_language, is_default, theme_id) values(
	1,
	'localhost:8000',
	'en',
	1,
	1
);

insert or ignore into page(id, server_id, relative_path, last_modified) values(
	1,
	1,
	'/',
	strftime('%s', 'now')
);

insert or ignore into page_content(id, page_id, language, title, content) values(
	1,
	1,
	'en',
	'Welcome',
	'## Hi there
Welcome to the default language page of the default server. You should add some content

Here is a second paragraph, with _italics_ and *emphasis*.

	'
);

insert or ignore into static_resources(id, server_id, key, value, content_type) values (
	1,
	1,
	'main.css',
	'
	body {
	color: blue;
	}
	',
	'text/css'
);

-- Database settings
pragma foreign_keys = on;
