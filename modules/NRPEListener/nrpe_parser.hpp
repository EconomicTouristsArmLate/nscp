#include <nrpe/nrpepacket.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/noncopyable.hpp>

namespace nrpe {
	namespace server {
		class parser : public boost::noncopyable{
			std::vector<char> buffer_;
			unsigned int packet_length_;
			unsigned int payload_length_;
		public:
			parser(unsigned int payload_length) 
				: payload_length_(payload_length)
				, packet_length_(nrpe::length::get_packet_length(payload_length))
			{
				std::cout << "== ctor length: " << payload_length_ << "/" << packet_length_ << std::endl;
			}

			template <typename InputIterator>
			boost::tuple<bool, InputIterator> digest(InputIterator begin, InputIterator end) {
				std::cout << "== Parsing: " << payload_length_ << "/" << packet_length_ << std::endl;
				int count = packet_length_ - buffer_.size();
				std::cout << "Bytes already: " << buffer_.size() << ", need more: " << count << " got: " << (end-begin) << std::endl;
				for (; count > 0&& begin != end; ++begin, --count)
					buffer_.push_back(*begin);
				return boost::make_tuple(buffer_.size() >= packet_length_, begin);
			}

			nrpe::packet parse() {
				nrpe::packet packet(buffer_, payload_length_);
				buffer_.clear();
				return packet;
			}
			void set_payload_length(unsigned int length) {
				payload_length_ = length;
				packet_length_ = nrpe::length::get_packet_length(length);
				std::cout << "== Setting length: " << payload_length_ << "/" << packet_length_ << std::endl;
			}
		};

	}// namespace server
} // namespace nrpe
