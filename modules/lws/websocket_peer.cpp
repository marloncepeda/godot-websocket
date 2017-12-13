#include "websocket_peer.h"
#include "core/io/ip.h"

void WebSocketPeer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_write_mode"), &WebSocketPeer::get_write_mode);
	ClassDB::bind_method(D_METHOD("set_write_mode", "mode"), &WebSocketPeer::set_write_mode);
	ClassDB::bind_method(D_METHOD("is_connected_to_host"), &WebSocketPeer::is_connected_to_host);
	ClassDB::bind_method(D_METHOD("is_binary_frame"), &WebSocketPeer::is_binary_frame);
	ClassDB::bind_method(D_METHOD("is_first_fragment"), &WebSocketPeer::is_first_fragment);
	ClassDB::bind_method(D_METHOD("is_final_fragment"), &WebSocketPeer::is_final_fragment);
	ClassDB::bind_method(D_METHOD("close"), &WebSocketPeer::close);

	BIND_ENUM_CONSTANT(WRITE_MODE_TEXT);
	BIND_ENUM_CONSTANT(WRITE_MODE_BINARY);
}

void WebSocketPeer::set_wsi(struct lws *p_wsi) {
	wsi = p_wsi;
};

void WebSocketPeer::set_write_mode(WriteMode p_mode) {
	write_mode = p_mode;
}

WebSocketPeer::WriteMode WebSocketPeer::get_write_mode() const {
	return write_mode;
}

Error WebSocketPeer::read_wsi(void *in, size_t len) {

	ERR_FAIL_COND_V(!is_connected_to_host(), FAILED);

	PeerData *peer_data = (PeerData *)(lws_wsi_user(wsi));
	int size = peer_data->in_size;

	if (peer_data->rbr.space_left() < len) {
		ERR_EXPLAIN("Buffer full! Dropping data");
		ERR_FAIL_V(FAILED);
	}

	copymem(&(peer_data->input_buffer[size]), in, len);
	size += len;

	peer_data->in_size = size;
	if (lws_is_final_fragment(wsi)) {
		peer_data->rbr.write((uint8_t *)(&size), 4);
		peer_data->rbr.write(peer_data->input_buffer, size);
		peer_data->in_count++;
		peer_data->in_size = 0;
	}

	return OK;
}

Error WebSocketPeer::write_wsi() {

	ERR_FAIL_COND_V(!is_connected_to_host(), FAILED);

	PeerData *peer_data = (PeerData *)(lws_wsi_user(wsi));
	PoolVector<uint8_t> tmp;
	int left = peer_data->rbw.data_left();
	uint32_t to_write = 0;

	if (left == 0 || peer_data->out_count == 0)
		return OK;

	peer_data->rbw.read((uint8_t *)&to_write, 4);
	peer_data->out_count--;

	if (left < to_write) {
		peer_data->rbw.advance_read(left);
		return FAILED;
	}

	tmp.resize(LWS_PRE + to_write);
	peer_data->rbw.read(&(tmp.write()[LWS_PRE]), to_write);
	lws_write(wsi, &(tmp.write()[LWS_PRE]), to_write, (enum lws_write_protocol)write_mode);
	tmp.resize(0);

	if (peer_data->out_count > 0)
		lws_callback_on_writable(wsi); // we want to write more!

	return OK;
}

Error WebSocketPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {

	ERR_FAIL_COND_V(!is_connected_to_host(), FAILED);

	PeerData *peer_data = (PeerData *)lws_wsi_user(wsi);
	peer_data->rbw.write((uint8_t *)&p_buffer_size, 4);
	peer_data->rbw.write(p_buffer, MIN(p_buffer_size, peer_data->rbw.space_left()));
	peer_data->out_count++;

	lws_callback_on_writable(wsi); // notify that we want to write
	return OK;
};

Error WebSocketPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) const {

	ERR_FAIL_COND_V(!is_connected_to_host(), FAILED);

	PeerData *peer_data = (PeerData *)lws_wsi_user(wsi);

	if (peer_data->in_count == 0)
		return ERR_UNAVAILABLE;

	uint32_t to_read = 0;
	uint32_t left = 0;
	r_buffer_size = 0;

	peer_data->rbr.read((uint8_t *)&to_read, 4);
	peer_data->in_count--;
	left = peer_data->rbr.data_left();

	if(left < to_read) {
		peer_data->rbr.advance_read(left);
		return FAILED;
	}

	peer_data->rbr.read(packet_buffer, to_read);
	*r_buffer = packet_buffer;
	r_buffer_size = to_read;

	return OK;
};

int WebSocketPeer::get_available_packet_count() const {

	if (!is_connected_to_host())
		return 0;

	return ((PeerData *)lws_wsi_user(wsi))->in_count;
};

bool WebSocketPeer::is_binary_frame() const {

	ERR_FAIL_COND_V(!is_connected_to_host(), false);

	return lws_frame_is_binary(wsi);
};

bool WebSocketPeer::is_final_fragment() const {

	ERR_FAIL_COND_V(!is_connected_to_host(), false);

	return lws_is_final_fragment(wsi);
};

bool WebSocketPeer::is_first_fragment() const {

	ERR_FAIL_COND_V(!is_connected_to_host(), false);

	return lws_is_first_fragment(wsi);
};

bool WebSocketPeer::is_connected_to_host() const {

	return wsi != NULL;
};

void WebSocketPeer::close() {
	if (wsi != NULL) {
		struct lws *tmp = wsi;
		PeerData *data = ((PeerData *)lws_wsi_user(wsi));
		data->force_close = true;
		wsi = NULL;
		lws_callback_on_writable(tmp); // notify that we want to disconnect
	}
};

IP_Address WebSocketPeer::get_connected_host() const {

	return IP_Address();
};

uint16_t WebSocketPeer::get_connected_port() const {

	return 1025;
};

WebSocketPeer::WebSocketPeer() {
	wsi = NULL;
	write_mode = WRITE_MODE_BINARY;
};

WebSocketPeer::~WebSocketPeer() {

	close();
};
