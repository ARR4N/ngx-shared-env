#nginx shared hosting module (ngx_shared_env)

[Status](#status) | [Introduction](#intro) | [Setup](#setup) | [Configuration Directives](#config) | [Discussion Points](#discussion)

* [set_shared_env_directory](#set_shared_env_directory)
* [set_shared_env_owner](#set_shared_env_owner)
* [set_shared_env_fpm_port](#set_shared_env_fpm_port)
* [set_shared_env_file_contents](#set_shared_env_file_contents)
* [set_shared_env_handler](#set_shared_env_handler)

##<a id="status"></a>Status
I am currently using this module in a production environment.

##<a id="intro"></a>Introduction
nginx's low-resource footprint promises to reduce infrastructure costs for hosting providers. However, the elements of the nginx architecture that make it resource-light also limit usability in shared hosting environments:

1. Configuration directives are parsed at launch and inclusion of new domains requires either a server restart or usage of regular expression server names. The latter fails to provide an adequate solution in a multi-user environment.

* PHP handling with [FPM](http://php-fpm.org/) is a proven method but security concerns arise regarding user access controls. FPM pools (along with proper directory permissions) address these concerns although on-the-fly delegation to the correct pool needs to be addressed.

* Apache's mod_rewrite and .htaccess files allow users to stipulate content-handling directives. However, parsing of per-directory configuration is resource intensive.

I have attempted to address each of these points through this module as well as properly constructed nginx / php5-fpm configurations and security-oriented filesystem access.

It is important to recognise that the convenience of unchanging config files comes at the expense of some increased resource usage. My experience has been that this per-request overhead is still a great improvement over Apache.

##<a id="setup"></a>Setup
The module assumes the following:

1. Linux
* The [NDK](https://github.com/simpl/ngx_devel_kit) module
* A group *fpmusers*, the members of which, each have a directory */var/www/public/username*. Directory ownership is *username:www-data* and access permissions are 770 (discussed later).
    1. Domain names are mapped to the filesystem through the similarity of domain and directory hierarchies: *www.example.com* -> *com/example/www* (see [*set_shared_env_directory*](#set_shared_env_directory) directive).
    * Public HTML directories *_public* e.g. *com/example/www/_public*
* nginx running as *www-data:www-data* and installed in */usr/local/nginx*. The directory */usr/local/nginx/conf/ownercache* (see [*set_shared_env_owner*](#set_shared_env_owner) directive) should be owned by *www-data:www-data* with write permission set for the owner only.

I plan to include [Puppet](http://puppetlabs.com/) and/or [Chef](http://www.opscode.com/chef/) setups at a later date. Contributions would be greatly appreciated.

##<a id="config"></a>Configuration Directives

###<a id="set_shared_env_directory"></a>set_shared_env_directory
    syntax: set_shared_env_directory $dir $host;
Maps domain names to the file system by effectively splitting into an array of domain parts, reversing the array, and joining into a directory structure e.g. if *$host* contains *www.example.com*, *$dir* will be set to *com/example/www*&mdash;to ignore the *www*, a regular expression server name can be used:

    server_name ~^(?<www>www\.)?(?<domain>[a-zA-Z0-9-\.]+)$;
    set_shared_env_directory $dir $domain;

###<a id="set_shared_env_owner"></a>set_shared_env_owner
    syntax: set_shared_env_directory $owner $dir;
    where: $dir was set by set_shared_env_directory
If no cached value can be found then the users' directories in */var/www/public/* are scanned for the existence of *$dir/_public* and the result is cached before returning.

###<a id="set_shared_env_fpm_port"></a>set_shared_env_fpm_port
    syntax: set_shared_env_fpm_port $fpmport $owner;
    where: $owner was set by set_shared_env_owner
The owner's UID is added to a base number (40000 was chosen) that provides an otherwise unused region in the port space. The users' FPM pools are expected to listen on the respective ports. This approach provides a programmatic means of on-the-fly pool delegation without the need to restart nginx for each new user. *getpwnam* or *id -u* can be used by php5-fpm configuration scripts to find a user's UID.

    fastcgi_pass   127.0.0.1:${fpmport};

###<a id="set_shared_env_file_contents"></a>set_shared_env_file_contents
    syntax: set_shared_env_file_contents $var $path;
As expected, reads the contents of *$path* into *$var*.

###<a id="set_shared_env_handler"></a>set_shared_env_handler
    syntax: set_shared_env_handler $handler "${owner}/${dir}" "handler1" ["handler2" ... ["handler5"]]
    where: $owner and $dir were set by set_shared_env_owner and set_shared_env_directory respectively

Each of the (up to 5) handler names is used to check for the existence of a "flag file" in the relevant directory (flags will be siblings of *_public* in the directory structure). Flags should be named *_handlername* and once the first file is found, the name is returned. If none of the listed handlers is found, "404" is returned. This can then be used as follows (note the use of the variable named location **@$**handler):

    set_shared_env_directory $dir $host; # host is set by the core module
    set_shared_env_owner $owner $dir;
    set_shared_env_handler $handler "${owner}/${dir}" "central_index" "dummy_backend";
    
    root "/var/www/public/${owner}/${dir}/_public";
    
    location @404 {
        return 404;
    }
    
    # this is a common pattern used by WordPress, Joomla, etc. and replaces the need for .htaccess rewrites
    location @central_index {
        rewrite . index.php?q=$uri&$args;
    }
    
    location @dummy_backend {
        proxy_pass ...
        ...
    }
    
    location ~ \.php$ {
        try_files $uri @$handler;
        set_shared_env_fpm_port $fpmport $owner;
        fastcgi_pass    127.0.0.1:${fpmport}
        ... # other fastcgi directives
    }
    
    location / {
        try_files $uri @$handler;
    }

##<a id="discussion"></a>Discussion Points
Coming soon

* Optimisation / scaling / overhead minimisation
* Security