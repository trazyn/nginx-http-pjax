
( function( $, undefined ) {

	"use strict";

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
	
		var state, deferred;

		url = typeof url === "string" ? url : window.location.pathname;

		state = cacheEntries[ cacheMapping[ url ] ];

		request && request.readyState < 4 && request.abort();

		if ( state && state.content ) {

			window.history.replaceState( "", "", url );
			settings.container.html( state.content );
		} else {
			deferred = $.Deferred();

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

				deferred.resolveWith( state );

				if ( settings.cache && settings.maxCacheLength ) {
					cachePush( state );
				}

				if ( typeof settings.after === "function" ) {
					
					settings.after( settings );
				}
			} );
		}

		$.when( deferred ).done( function() {
			settings.after instanceof Function && settings.after( settings );
		} );

		return deferred;
	}

	$.support.pjax = 
		window.history && window.history.pushState && window.history.replaceState &&
		/** pushState isn't reliable in IOS until 5 */
		!navigator.userAgent.match( /((iPod|iPhone|iPad).+\bOS\s+[1-4]\D|WebApps\/.+CFNetwork)/ );

	$.fn.pjax = !$.support.pjax ? $.noop : function( selector, container, options ) {
		
		var 
		state;

		if ( options === undefined ) {
			
			switch ( typeof container ) {
				
				case "string":
					settings = $.fn.pjax.defaults;
					settings.container = container = $( container );
					break;

				case "object":
					settings = $.extend( true, {}, $.fn.pjax.defaults, container );
					settings.container = container = $( settings.container );
					break;
			}
		}

		$( window ).on( "popstate.pjax", process  );
		
		return this
			.delegate( selector, "click.pjax", function( e ) {

				var
				url,
				promise,
				link = e.target;
				
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

				url = link.getAttribute( "href" );

				e.preventDefault();
				e.stopPropagation();

				promise = process( url );

				promise && promise.done( function() {
					
					if ( settings.push || settings.replace ) {
						window.history[ settings.push ? "pushState" : "replaceState" ]( this, document.title, url );
					}
				} );

				document.body.scrollTop = 0;
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
