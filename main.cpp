#include <iostream>

#include <restinio/all.hpp>
#include <restinio/websocket/websocket.hpp>
#include <fmt/format.h>
#include <json_dto/pub.hpp>

struct place_t
{
	place_t() = default;

	place_t(
		std::string placeName,
		std::string placeLat,
		std::string placeLon )
		:	m_placeName{ std::move( placeName ) }
		,	m_placeLat{ std::move( placeLat ) }
		,	m_placeLon{ std::move( placeLon ) }
	{}

	template < typename JSON_IO >
	void
	json_io( JSON_IO & io )
	{
		io
			& json_dto::mandatory( "PlaceName", m_placeName )
			& json_dto::mandatory( "PlaceLat", m_placeLat )
			& json_dto::mandatory( "PlaceLon", m_placeLon );
	}

	std::string m_placeName;
	std::string m_placeLat;
	std::string m_placeLon;
};

struct weather_station_t
{
	weather_station_t() = default;

	weather_station_t(
		std::string ID,
		std::string dato,
		std::string klokkeslaet,
		place_t sted,
		std::string temperatur,
		std::string luftfugtighed )
		:	m_ID{ std::move( ID ) }
		,	m_dato{ std::move( dato ) }
		,	m_klokkeslaet{ std::move( klokkeslaet ) }
		,	m_sted{ std::move( sted ) }
		,	m_temperatur{ std::move( temperatur ) }
		,	m_luftfugtighed{ std::move( luftfugtighed ) }
	{}

	template < typename JSON_IO >
	void
	json_io( JSON_IO & io )
	{
		io
			& json_dto::mandatory( "ID", m_ID )
			& json_dto::mandatory( "dato", m_dato )
			& json_dto::mandatory( "klokkeslaet", m_klokkeslaet )
			& json_dto::mandatory( "sted", m_sted )
			& json_dto::mandatory( "temperatur", m_temperatur )
			& json_dto::mandatory( "luftfugtighed", m_luftfugtighed );
	}

	std::string m_ID;
	std::string m_dato;
	std::string m_klokkeslaet;
	place_t m_sted;
	std::string m_temperatur;
	std::string m_luftfugtighed;
};

using weather_station_collection_t = std::vector< weather_station_t >;

namespace rr = restinio::router;
using router_t = rr::express_router_t<>;

//******************** Create websocket *******************

namespace websocket_ws = restinio::websocket::basic;

using traits_t = 
restinio::traits_t
<
	restinio::asio_timer_manager_t,
	restinio::single_threaded_ostream_logger_t,
	router_t
>;

using ws_registry_t = std::map<std::uint64_t, websocket_ws::ws_handle_t>;

//*********************************************************
class weather_handler_t
{
public :
	explicit weather_handler_t( weather_station_collection_t & weather_stations )
		:	m_weather_stations( weather_stations )
	{}

	weather_handler_t( const weather_handler_t & ) = delete;
	weather_handler_t( weather_handler_t && ) = delete;

	auto on_weather_list(
		const restinio::request_handle_t& req, rr::route_params_t ) const
	{
		auto resp = init_resp( req->create_response() );

		resp.set_body(json_dto::to_json(m_weather_stations));

		return resp.done();
	}

	auto lastThree(
		const restinio::request_handle_t& req, rr::route_params_t ) const
	{
		auto resp = init_resp( req->create_response() );


		if (m_weather_stations.size() > 0)
		{
			resp.set_body(json_dto::to_json(m_weather_stations.back()));
		}
		if (m_weather_stations.size() > 1)
		{
			resp.set_body(json_dto::to_json(m_weather_stations.back()));
			resp.append_body(",");
			resp.append_body(json_dto::to_json(m_weather_stations[m_weather_stations.size()-2]));
		}
		if (m_weather_stations.size() > 2)
		{
			resp.set_body(json_dto::to_json(m_weather_stations.back()));
			resp.append_body(",");
			resp.append_body(json_dto::to_json(m_weather_stations[m_weather_stations.size()-2]));
			resp.append_body(",");
			resp.append_body(json_dto::to_json(m_weather_stations[m_weather_stations.size()-3]));
		}

		return resp.done();
	}

	auto on_ws_get(
		const restinio::request_handle_t& req, rr::route_params_t params )
	{
		const auto weathernum = restinio::cast_to< std::uint32_t >( params[ "weathernum" ] );

		auto resp = init_resp( req->create_response() );

		if( 0 != weathernum && weathernum <= m_weather_stations.size() )
		{
			resp.set_body(json_dto::to_json(m_weather_stations[weathernum]));
		}
		else
		{
			resp.set_body(
				"No weather station with #" + std::to_string( weathernum ) + "\n" );
		}

		return resp.done();
	}

	auto on_new_weather_station(
		const restinio::request_handle_t& req, rr::route_params_t )
	{
		auto resp = init_resp( req->create_response() );

		try
		{
			// når vi kører http_post, så tilføjer vi det til sidst i vectoren
			m_weather_stations.emplace_back(
				json_dto::from_json< weather_station_t >( req->body() ) );
			sendMessage("{ \"action\":\"new\", \"data\": " +  req->body() + "}");
		}
		catch( const std::exception & /*ex*/ )
		{
			mark_as_bad_request( resp );
		}		

		return resp.done();
	}

	auto on_ws_update(
		const restinio::request_handle_t& req, rr::route_params_t params )
	{
		const auto weathernum = restinio::cast_to< std::uint32_t >( params[ "weathernum" ] );

		auto resp = init_resp( req->create_response() );

		try
		{
			auto b = json_dto::from_json< weather_station_t >( req->body() );

			if( 0 != weathernum && weathernum <= m_weather_stations.size() )
			{
				m_weather_stations[ weathernum - 1 ] = b;
				sendMessage("{ \"action\":\"update\", \"data\": " +  req->body() + "}");
			}
			else
			{
				mark_as_bad_request( resp );
				resp.set_body( "No station with #" + std::to_string( weathernum ) + "\n" );
			}
		}
		catch( const std::exception & /*ex*/ )
		{
			mark_as_bad_request( resp );
		}

		return resp.done();
	}

	auto on_ws_delete(
		const restinio::request_handle_t& req, rr::route_params_t params )
	{
		const auto weathernum = restinio::cast_to< std::uint32_t >( params[ "weathernum" ] );

		auto resp = init_resp( req->create_response() );

		if( 0 != weathernum && weathernum <= m_weather_stations.size() )
		{
			const auto & b = m_weather_stations[ weathernum - 1 ];
			resp.set_body(
				"Delete weather station #" + std::to_string( weathernum ) + ": " +
					b.m_ID + "[" + b.m_ID + "]\n" );
			
			sendMessage("{ \"action\":\"delete\", \"data\": " +  req->body() + "}");
			m_weather_stations.erase( m_weather_stations.begin() + ( weathernum - 1 ) );
		}
		else
		{
			resp.set_body(
				"No weather station with #" + std::to_string( weathernum ) + "\n" );
		}

		return resp.done();
	}

	auto on_date_weather_get(
		const restinio::request_handle_t& req, rr::route_params_t params )
	{
		auto resp = init_resp( req->create_response() );
		try
		{
			auto Date = restinio::utils::unescape_percent_encoding( params[ "Date" ] );

			resp.set_body( "Weather registrations on this date: " + Date + ":\n\n" );

			for( std::size_t i = 0; i < m_weather_stations.size(); ++i )
			{
				const auto & b = m_weather_stations[ i ];
				if( Date == b.m_dato)
				{
					resp.append_body(json_dto::to_json(m_weather_stations[i]));
					resp.append_body(",");
				}
			}
			resp.append_body("\nEnd of weather registrations");
		}
		catch( const std::exception & )
		{
			mark_as_bad_request( resp );
		}

		return resp.done();
	}

	//********* reference til ngk slides --> Søren Hansen *************
	auto on_live_weather_list(
		const restinio::request_handle_t& req, rr::route_params_t params) 
	{
		if( restinio::http_connection_header_t::upgrade == req->header().connection() )
			{
				auto wsh =
					websocket_ws::upgrade< traits_t >(
						*req,
						websocket_ws::activation_t::immediate,
						[ this ]( auto wsh, auto m ){ //tilføjet this pointer
							if( websocket_ws::opcode_t::text_frame == m->opcode() ||
								websocket_ws::opcode_t::binary_frame == m->opcode() ||
								websocket_ws::opcode_t::continuation_frame == m->opcode() )
							{
								wsh->send_message(*m);
							}
							else if( websocket_ws::opcode_t::ping_frame == m->opcode() )
							{
								auto resp = *m;
								resp.set_opcode( websocket_ws::opcode_t::pong_frame );
								wsh->send_message( resp );
							}
							else if( websocket_ws::opcode_t::connection_close_frame == m->opcode() )
							{
								registry.erase( wsh->connection_id() );
							}
						} );

				registry.emplace( wsh->connection_id(), wsh );

				init_resp(req->create_response()).done(); //tilføjet ekstra
				return restinio::request_accepted();
			}
			return restinio::request_rejected();
	}

	auto options(restinio::request_handle_t req,
		restinio::router::route_params_t /*params*/)
	{
		const auto methods = "OPTIONS, GET, POST, PATCH, DELETE";
		auto resp = init_resp( req->create_response() );
		resp.append_header(restinio::http_field::access_control_allow_methods, methods);
		resp.append_header(restinio::http_field::access_control_allow_headers, "content-type");
		resp.append_header(restinio::http_field::access_control_max_age, "86400");
		return resp.done();
	}

	//**********************************************************************

private :
	weather_station_collection_t & m_weather_stations;
	ws_registry_t registry;

	template < typename RESP >
	static RESP
	init_resp( RESP resp )
	{
		resp
		.append_header( "Server", "RESTinio sample server /v.0.6" )
		.append_header_date_field()
		.append_header( "Content-Type", "application/json" )
		.append_header(restinio::http_field::access_control_allow_origin, "*");
		return resp;
	}

	template < typename RESP >
	static void
	mark_as_bad_request( RESP & resp )
	{
		resp.header().status_line( restinio::status_bad_request() );
	}
//********* reference til ngk slides --> Søren Hansen *************
// udskriver alle opdateringer til klienter
	void sendMessage(std::string message )
	{
		for (auto [k, v] : registry)
		{
			v->send_message(websocket_ws::final_frame,
							websocket_ws::opcode_t::text_frame,
							message);
		}
	}
//**********************************************************************
};

auto server_handler( weather_station_collection_t & weather_station_collection )
{
	auto router = std::make_unique< router_t >();
	auto handler = std::make_shared< weather_handler_t >( std::ref(weather_station_collection) );

	auto by = [&]( auto method ) {
		using namespace std::placeholders;
		return std::bind( method, handler, _1, _2 );
	};

	auto method_not_allowed = []( const auto & req, auto ) {
			return req->create_response( restinio::status_method_not_allowed() )
					.connection_close()
					.done();
		};


	//my new handlers
	/***********************************************************************/

	// Handler for '/last_three' path.
	router->http_get( "/last_three", by( &weather_handler_t::lastThree ) );

	// Disable all other methods for '/last_three'.
	router->add_handler(
			restinio::router::none_of_methods(
					restinio::http_method_get()),
			"/last_three", method_not_allowed );

	// Handler for '/date/:date' path.
	router->http_get( "/Date/:Date", by( &weather_handler_t::on_date_weather_get ) );

	// Disable all other methods for '/date/:date'.
	router->add_handler(
			restinio::router::none_of_methods( restinio::http_method_get() ),
			"/Date/:Date", method_not_allowed );

		// Handlers for '/live/' path.
	router->http_get( "/live", by( &weather_handler_t::on_live_weather_list ) );

	// Disable all other methods for '/live/'.
	router->add_handler(
			restinio::router::none_of_methods(
					restinio::http_method_get() ),
			"/live", method_not_allowed );
	/***********************************************************************/



	// Handlers for '/' path.
	router->http_get( "/", by( &weather_handler_t::on_weather_list ) );
	router->http_post( "/", by( &weather_handler_t::on_new_weather_station ) );
	router->add_handler(restinio::http_method_options(), "/", by( &weather_handler_t::options ) );

	// Disable all other methods for '/'.
	router->add_handler(
			restinio::router::none_of_methods(
					restinio::http_method_get(), restinio::http_method_post() ),
			"/", method_not_allowed );

	// Handler for '/author/:author' path.
	router->http_get( "/author/:author", by( &weather_handler_t::on_ws_get ) );

	// Disable all other methods for '/author/:author'.
	router->add_handler(
			restinio::router::none_of_methods( restinio::http_method_get() ),
			"/author/:author", method_not_allowed );

	// Handlers for '/:weathernum' path.
	router->http_get(
			R"(/:weathernum(\d+))",
			by( &weather_handler_t::on_ws_get ) );
	router->http_put(
			R"(/:weathernum(\d+))",
			by( &weather_handler_t::on_ws_update ) );
	router->http_delete(
			R"(/:weathernum(\d+))",
			by( &weather_handler_t::on_ws_delete ) );

	// Disable all other methods for '/:weathernum'.
	router->add_handler(
			restinio::router::none_of_methods(
					restinio::http_method_get(),
					restinio::http_method_post(),
					restinio::http_method_delete() ),
			R"(/:weathernum(\d+))", method_not_allowed );

	return router;
}

int main()
{
	using namespace std::chrono;

	try
	{
		using traits_t =
			restinio::traits_t<
				restinio::asio_timer_manager_t,
				restinio::single_threaded_ostream_logger_t,
				router_t >;

		weather_station_collection_t weather_station_collection{
			{"1", "20211105", "12:15", {"Aarhus N", "13.692", "19.438"}, "13.1", "70%"}};

		restinio::run(
			restinio::on_this_thread< traits_t >()
				.address( "localhost" )
				.request_handler( server_handler( weather_station_collection ) )
				.read_next_http_message_timelimit( 10s )
				.write_http_response_timelimit( 1s )
				.handle_request_timeout( 1s ) );
	}
	catch( const std::exception & ex )
	{
		std::cerr << "Error: " << ex.what() << std::endl;
		return 1;
	}

	return 0;
}
