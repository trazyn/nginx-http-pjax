
( function( $, undefined ) {

	var 

	settings,
	request,

	cacheMapping = {},
	cacheEntries = [];

	function cachePush( state ) {

		var index;
	
		if ( state.url ) {

			index = cacheMapping[ state.url ];

			if ( index !== undefined ) {
				cacheEntries[ index ] = state;
			} else {
			
				cacheEntries.push( state );

				if ( cacheEntries.length > settings.maxCacheLength ) {
					delete cacheMapping[ cacheEntries.pop()[ "url" ] ];
				}

				cacheMapping[ state.url ] = cacheEntries.length - 1;
			}
		}
	}

	function process( url ) {
	
		var state;

		url = typeof url === "string" ? url : window.location.pathname;

		state = cacheEntries[ cacheMapping[ url ] ];

		request && request.readyState < 4 && request.abort();

		if ( state && state.content ) {

			settings.container.html( state.content );
			window.history.pushState( state, state.title, state.url );
		} else {
			request = $.ajax( $.extend( {}, settings.ajaxOptions, {
				url: url + "?" + +new Date()
			} ) )
			.done( function( data ) {
			
				settings.container.html( data );

				state = {
					url: url,
					title: document.title,
					content: data
				};

				if ( settings.push || settings.replace ) {
					window.history[ settings.push ? "pushState" : "replaceState" ]( state, document.title, url )
				}

				if ( settings.cache && settings.maxCacheLength ) {
					cachePush( state );
				}
			} );
		}
	}

	$.support.pjax = 
		window.history && window.history.pushState && window.history.replaceState &&
		/** pushState isn't reliable in IOS until 5 */
		!navigator.userAgent.match( /((iPod|iPhone|iPad).+\bOS\s+[1-4]\D|WebApps\/.+CFNetwork)/ );

	$.fn.pjax = !$.support.pjax ? $.noop : function( selector, container, options ) {
		
		var 
		
		state = {
			id: window.location.pathname,
			title: document.title,
			url: window.location.href
		};

		if ( options === undefined ) {
			
			switch ( typeof container ) {
				
				case "string":
					settings = $.fn.pjax.defaults;
					settings.container = container = $( container );
					break;

				case "object":
					settings = $.extend( true, {}, $.fn.pjax.defaults, container );
					settings.cintainer = container = $( container );
					break;
			}
		}

		window.history.pushState( state, document.title, location.href );

		$( window ).on( "popstate.pjax", process  );
		
		return this
			.delegate( selector, "click.pjax", function( e ) {

				var link = e.target;
				
				/** Middle click, cmd click, and ctrl click should open links in a new tab as normal */
				if ( e.which > 1 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey 

					/** Require an anchor element */
					|| link.tagName.toUpperCase() !== "A"
					
					/** Ignore cross origin links */
					|| location.protocol !== link.protocol || location.hostname !== link.hostname

					/** Ignore case when a hash is being tacked on the current URL */
					|| link.href.indexOf( "#" ) === 0) {

					return;
				}

				e.preventDefault();
				e.stopPropagation();

				process( link.getAttribute( "href" ) );
			} );
	};

	$.fn.pjax.defaults = {

		container 	: "#pjax-container",
		maxCacheLength 	: 20,
		timeout 	: 3000,
		cache 		: true,
		push 		: true,
		replace 	: false,
		
		ajaxOptions: {

			type: "GET",
			dataType: "html",
			headers: {
				"X-PJAX": true
			}
		}
	};

} )( window.jQuery );
