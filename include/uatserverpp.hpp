#ifndef _UATSERVERPP_H_INCLUDED
#define _UATSERVERPP_H_INCLUDED

#if !defined(ASIO_STANDALONE) && !defined(ASIO_BOOST)
#define ASIO_STANDALONE
#endif
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <list>
#include <set>
#include <map>
#include <algorithm>
#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>

namespace UAT {

struct Var
{
    typedef nlohmann::json json;
    std::string slot;
    std::string name;
    json value;
    
    Var(){}
    Var(const std::string& sl, const std::string& na, const json& val)
        : slot(sl), name(na), value(val) {}
    
    json to_json() const {
        if (slot.empty()) {
            return {
                {"cmd", "Var"}, {"name", name}, {"value", value}
            };
        } else {
            return {
                {"cmd", "Var"}, {"slot", slot}, {"name", name}, {"value", value}
            };
        }
    }
};

class Server
{
public:
    static constexpr int DEFAULT_PORT = 65399;
    static constexpr int FALLBACK_PORT = 44444;
    static constexpr int PROTOCOL_VERSION = 0;

protected:
    typedef websocketpp::server<websocketpp::config::asio> WSServer;
    typedef nlohmann::json json;
    typedef valijson::adapters::NlohmannJsonAdapter JsonSchemaAdapter;

public:
    class CommandError : public std::exception
    {
        std::string _cmd;
        std::string _arg;
        std::string _reason;
        std::string _description;
        std::string _msg;
    public:
        CommandError(const std::string& cmd, const std::string& arg, const std::string& reason)
            : _cmd(cmd), _arg(arg), _reason(reason)
        {
            _msg = _arg.empty() ?
                   _reason + ": " + _cmd :
                   _reason + ": " + _cmd + "." + _arg;
        }
        CommandError(const std::string& cmd, const valijson::ValidationResults::Error& err)
            : _cmd(cmd), _description(err.description)
        {            
            if (err.context.size() < 2) { // error in root
                const char* p0 = err.description.c_str();
                const char* p2 = p0 + err.description.length() - 2;
                if (p2>p0 && *p2 == '\'' && *(p2+1) == '.') {
                    const char* p1 = p2-1;
                    while (p1>p0 && *p1 != '\'') p1--;
                    if (*p1 == '\'') _arg = std::string(p1+1, p2-p1-1);
                }
                _reason = _arg.empty() ? "unknown" : "missing agument";
            } else { // error in property = argument
                _arg = err.context[1];
                _arg = _arg.substr(1, _arg.length()-2);
                _reason = "bad value";
            }

            _msg = _arg.empty() ?
                   _reason + ": " + _cmd + ": " + _description:
                   _reason + ": " + _cmd + "." + _arg + ": " + _description;
        }
        virtual const char* what() const noexcept override { return _msg.c_str(); }
        virtual json to_json() const {
            json j = { {"cmd", "ErrorReply"}, {"name", _cmd}, {"reason", _reason} };
            if (!_arg.empty()) j["argument"] = _arg;
            if (!_description.empty()) j["description"] = _description;
            return j;
        }
    };

protected:
    WSServer _wss;
    std::list<WSServer::connection_ptr> _connections;
    std::string _name;
    std::string _version;
    std::map<std::string, std::map<std::string, json> > _vars;
    const json _packetSchemaJson = R"({
        "type": "array",
        "items": {
            "type": "object",
            "properties": {
                "cmd": { "type": "string" }
            },
            "required": [ "cmd" ]
        }
    })"_json;
    const json _syncSchemaJson = R"({
        "type": "object",
        "properties": {
            "slot": { "type": "string" }
        }
    })"_json;
    valijson::Schema _packetSchema;
    valijson::Schema _syncSchema;

    void log(const std::string& msg)
    {
        _wss.get_alog().write(websocketpp::log::alevel::app, "UAT: " + msg);
    }

    void on_message(websocketpp::connection_hdl hdl, WSServer::message_ptr msg)
    {
        log("message");
        try {
            json packet = json::parse(msg->get_payload());
            valijson::Validator validator;
            json reply = {};

            // FIXME: it is undefined what to do if we encounter a bad command
            //        inside an othervise valid packet. For now we validate
            //        individual commands and accept the rest
            JsonSchemaAdapter packetAdapter(packet);
            if (!validator.validate(_packetSchema, packetAdapter, NULL)) {
                throw std::runtime_error("Packet validation failed");
            }

            for (auto& command: packet) {
                std::string cmd = command["cmd"];
                JsonSchemaAdapter commandAdapter(command);
                valijson::ValidationResults res;
                valijson::ValidationResults::Error err;
                try {
                    if (cmd == "Sync") {
                        if (!validator.validate(_syncSchema, commandAdapter, &res)) {
                            if (res.popError(err)) {
                                throw CommandError(cmd, err);
                            }
                            throw CommandError(cmd, "", "unknown");
                        }
                        if (command["slot"].is_string() && command["slot"] != "") {
                            const auto it = _vars.find(command["slot"]);
                            if (it != _vars.end()) {
                                for (const auto& pair: it->second) {
                                    reply.push_back(Var(it->first, pair.first, pair.second).to_json());
                                }
                            } else {
                                throw CommandError(cmd, "slot", "bad value");
                            }
                        } else {
                            for (const auto& slotVarsPair: _vars) {
                                for (const auto& pair: slotVarsPair.second) {
                                    reply.push_back(Var(slotVarsPair.first, pair.first, pair.second).to_json());
                                }
                            }
                        }
                    } else {
                        throw CommandError(cmd, "", "unnknown cmd");
                    }
                } catch (CommandError& ex) {
                    reply.push_back(ex.to_json());
                } catch (std::exception& ex) {
                    throw std::runtime_error("Exception while handling " + cmd +
                                             ": " + ex.what());
                }
            }
            if (!reply.empty()) _wss.send(hdl, reply.dump(), websocketpp::frame::opcode::text);
        } catch (std::exception& ex) {
            _wss.get_con_from_hdl(hdl)->close(websocketpp::close::status::invalid_subprotocol_data, ex.what());
        }
    }

    void on_open(websocketpp::connection_hdl hdl)
    {
        log("open");

        _connections.push_back(_wss.get_con_from_hdl(hdl));

        std::list<std::string> slots;
        for (const auto& pair: _vars) if (!pair.first.empty()) slots.push_back(pair.first);
        json info = { {
            {"cmd", "Info"},
            {"name", _name},
            {"version", _version},
            {"protocol", PROTOCOL_VERSION},
            {"slots", slots},
        }, };
        _wss.send(hdl, info.dump(), websocketpp::frame::opcode::text);
    }

    void on_close(websocketpp::connection_hdl hdl)
    {
        log("close");
        _connections.remove(_wss.get_con_from_hdl(hdl));
    }

    template <class K, class T, class C, class A, class Predicate>
    void erase_if(std::map<K, T, C, A>& c, Predicate pred) {
        for (auto i = c.begin(), last = c.end(); i != last; )
            if (pred(*i))
                i = c.erase(i);
            else
                ++i;
    }

public:
    Server(asio::io_service* service)
    {
        valijson::SchemaParser parser;
        parser.populateSchema(JsonSchemaAdapter(_packetSchemaJson), _packetSchema);
        parser.populateSchema(JsonSchemaAdapter(_syncSchemaJson), _syncSchema);

        _wss.clear_access_channels(websocketpp::log::alevel::frame_header);
        _wss.clear_access_channels(websocketpp::log::alevel::frame_payload);
        _wss.clear_access_channels(websocketpp::log::alevel::control);
        _wss.init_asio(service);
        _wss.set_message_handler([this] (websocketpp::connection_hdl hdl, WSServer::message_ptr msg) {
            this->on_message(hdl,msg);
        });
        _wss.set_open_handler([this] (websocketpp::connection_hdl hdl) {
            this->on_open(hdl);
        });
        _wss.set_close_handler([this] (websocketpp::connection_hdl hdl) {
            this->on_close(hdl);
        });
        _wss.set_reuse_addr(true);
    }

    virtual ~Server()
    {
    }

    bool start()
    {
        if (_wss.transport_type::is_listening()) return true;
        auto ports = { DEFAULT_PORT, FALLBACK_PORT };
        for (auto& port : ports) {
            try {
                _wss.listen(port);
                _wss.start_accept();
                log("listening on " + std::to_string(port));
                break;
            } catch (websocketpp::exception& ex) {
                log("could not listen on " + std::to_string(port));
                if (&port == ports.end()-1) {
                    return false; //throw ex;
                }
            }
        }
        return true;
    }

    void stop()
    {
        // NOTE: run this on the same service (io_service::post) or add locking
        log("stop listen");
        _wss.stop_listening();
        log("closing connections");
        for (auto& conn: _connections) {
            conn->close(websocketpp::close::status::going_away, "");
        }
    }

    void set_name(const std::string& name)
    {
        _name = name;
    }

    void set_version(const std::string& version)
    {
        _version = version;
    }

    void set_slots(const std::set<std::string>& slots)
    {
        // NOTE: run this on the same service (io_service::post) or add locking
        erase_if(_vars, [&slots](const auto& pair) { return slots.find(pair.first) == slots.end(); });
        for (const auto& slot: slots) {
            if (_vars.find(slot) == _vars.end())
                _vars[slot] = {};
        }
    }

    void set_vars(const std::list<Var>& vars)
    {
        // NOTE: run this on the same service (io_service::post) or add locking
        auto changes = json::array();
        for (const auto& var: vars) {
            auto slotIt = _vars.find(var.slot);
            if (slotIt == _vars.end()) {
                // new slot
                changes.push_back(var.to_json());
                _vars[var.slot] = { {var.name, var.value} };
            } else {
                auto varIt = slotIt->second.find(var.name);
                if (varIt == slotIt->second.end()) {
                    // new variable
                    changes.push_back(var.to_json());
                    slotIt->second[var.name] = var.value;
                } else {
                    if (varIt->second != var.value) {
                        // value changed
                        changes.push_back(var.to_json());
                        varIt->second = var.value;
                    }
                }
            }
        }
        if (!changes.empty()) {
            for (auto& conn: _connections) {
                conn->send(changes.dump(), websocketpp::frame::opcode::text);
            }
        }
    }
};

} // namespace UAT


#endif // _UATSERVERPP_H_INCLUDED
