/*
 * SPDX-License-Identifier: MIT
 */

#include "nfc_ndef.h"

namespace nfc {
namespace {

// Tabla de prefijos de URI del NFC Forum (RTD URI). El primer byte del payload de un
// registro "U" es el índice en esta tabla; el resto es la cola de la URI.
const char* const kUriPrefixes[] = {
    "",        "http://www.",  "https://www.", "http://",
    "https://", "tel:",        "mailto:",      "ftp://anonymous:anonymous@",
    "ftp://ftp.", "ftps://",    "sftp://",      "smb://",
    "nfs://",  "ftp://",        "dav://",       "news:",
    "telnet://", "imap:",       "rtsp://",      "urn:",
    "pop:",    "sip:",          "sips:",        "tftp:",
    "btspp://", "btl2cap://",   "btgoep://",    "tcpobex://",
    "irdaobex://", "file://",   "urn:epc:id:",  "urn:epc:tag:",
    "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};
constexpr std::size_t kUriPrefixCount = sizeof(kUriPrefixes) / sizeof(kUriPrefixes[0]);

std::string bytes_to_string(const uint8_t* data, std::size_t len) {
    return std::string(reinterpret_cast<const char*>(data), len);
}

} // namespace

bool parse_ndef_message(const uint8_t* data, std::size_t len, NdefMessage& out) {
    out.records.clear();
    std::size_t i = 0;

    while (i < len) {
        const uint8_t header = data[i++];
        const uint8_t tnf = header & 0x07;
        const bool short_record = (header & 0x10) != 0; // SR
        const bool has_id = (header & 0x08) != 0;       // IL
        const bool message_end = (header & 0x40) != 0;  // ME

        if (i >= len) {
            return false;
        }
        const uint8_t type_len = data[i++];

        std::size_t payload_len = 0;
        if (short_record) {
            if (i >= len) {
                return false;
            }
            payload_len = data[i++];
        } else {
            if (i + 4 > len) {
                return false;
            }
            payload_len = (static_cast<std::size_t>(data[i]) << 24) |
                          (static_cast<std::size_t>(data[i + 1]) << 16) |
                          (static_cast<std::size_t>(data[i + 2]) << 8) |
                          static_cast<std::size_t>(data[i + 3]);
            i += 4;
        }

        std::size_t id_len = 0;
        if (has_id) {
            if (i >= len) {
                return false;
            }
            id_len = data[i++];
        }

        if (i + type_len + id_len + payload_len > len) {
            return false; // cabecera que promete más de lo que hay
        }

        const std::string type = bytes_to_string(data + i, type_len);
        i += type_len;
        i += id_len; // el ID no lo usamos
        const uint8_t* payload = data + i;
        i += payload_len;

        NdefRecord record;
        // TNF 0x01 = tipo conocido del NFC Forum ("U", "T"…).
        if (tnf == 0x01 && type == "U" && payload_len >= 1) {
            const uint8_t prefix_index = payload[0];
            std::string uri = (prefix_index < kUriPrefixCount) ? kUriPrefixes[prefix_index] : "";
            uri += bytes_to_string(payload + 1, payload_len - 1);
            record.type = NdefRecordType::Uri;
            record.content = uri;
        } else if (tnf == 0x01 && type == "T" && payload_len >= 1) {
            const uint8_t status = payload[0];
            const std::size_t lang_len = status & 0x3F;
            if (1 + lang_len > payload_len) {
                return false;
            }
            record.type = NdefRecordType::Text;
            record.language = bytes_to_string(payload + 1, lang_len);
            record.content = bytes_to_string(payload + 1 + lang_len, payload_len - 1 - lang_len);
        } else if (tnf == 0x00 && type_len == 0 && payload_len == 0) {
            record.type = NdefRecordType::Empty;
        } else {
            record.type = NdefRecordType::Other;
            record.type_name = type;
        }
        out.records.push_back(std::move(record));

        if (message_end) {
            break;
        }
    }

    return !out.records.empty();
}

bool parse_type2_ndef(const std::vector<uint8_t>& memory, NdefMessage& out, std::size_t data_start) {
    std::size_t i = data_start;
    const std::size_t n = memory.size();

    while (i < n) {
        const uint8_t tag = memory[i++];
        if (tag == 0x00) {
            continue; // NULL TLV: relleno
        }
        if (tag == 0xFE) {
            break; // terminador
        }
        if (i >= n) {
            return false;
        }

        std::size_t length = memory[i++];
        if (length == 0xFF) { // longitud en 3 bytes
            if (i + 2 > n) {
                return false;
            }
            length = (static_cast<std::size_t>(memory[i]) << 8) | memory[i + 1];
            i += 2;
        }

        if (i + length > n) {
            return false;
        }

        if (tag == 0x03) { // mensaje NDEF
            return parse_ndef_message(memory.data() + i, length, out);
        }
        i += length; // otros TLV (lock, memory control, propietario): se saltan
    }

    return false; // no había TLV NDEF
}

} // namespace nfc
