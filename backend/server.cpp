/*
 ════════════════════════════════════════════════════════════
   UNIVERSITY CAFETERIA — server.cpp
   Handles: /ping  /calculate  /order  /summary

   The browser frontend (index.html / app.js) sends JSON
   requests to this server. ALL bill calculations are done
   here in C++ — subtotal, tax, grand total, averages, etc.
 ════════════════════════════════════════════════════════════
*/

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <thread>
#include <mutex>
#include <cstdlib>

// ── Platform-specific socket headers ────────────────────
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <arpa/inet.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR   (-1)
  #define closesocket(s) close(s)
#endif

// ── Constants ────────────────────────────────────────────
constexpr double TAX_RATE = 0.05;
constexpr int    BACKLOG  = 10;

// ═══════════════════════════════════════════════════════════
//  DATA STRUCTURES
// ═══════════════════════════════════════════════════════════
struct OrderItem {
    int         id;
    std::string name;
    int         qty;
    double      price;
    double      rowTotal() const { return price * qty; }
};

struct Order {
    int                     orderId;
    std::string             customer;
    std::vector<OrderItem>  items;
    double                  subtotal;
    double                  tax;
    double                  grandTotal;
    std::string             time;
};

struct BillResult {
    int    itemCount;
    double subtotal;
    double tax;
    double grandTotal;
};

// ── Global state (protected by mutex) ───────────────────
std::vector<Order> g_orders;
std::mutex         g_mutex;
int                g_nextId = 1;

// ═══════════════════════════════════════════════════════════
//  CORE CALCULATION ENGINE  (the heart of the C++ backend)
// ═══════════════════════════════════════════════════════════

BillResult calculateBill(const std::vector<OrderItem>& items) {
    BillResult r{};
    for (const auto& item : items) {
        r.subtotal  += item.rowTotal();
        r.itemCount += item.qty;
    }
    r.tax       = std::round(r.subtotal * TAX_RATE);
    r.grandTotal = r.subtotal + r.tax;
    return r;
}

std::string currentTime() {
    std::time_t now = std::time(nullptr);
    char buf[10];
    std::strftime(buf, sizeof(buf), "%H:%M", std::localtime(&now));
    return buf;
}

// ═══════════════════════════════════════════════════════════
//  MINIMAL JSON HELPERS  (no external library needed)
// ═══════════════════════════════════════════════════════════

/* Escape a string for JSON */
std::string jsonStr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else            out += c;
    }
    return out + "\"";
}

/* Very small JSON parser — extracts string value for a key */
std::string jsonGet(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\r\n:", pos);
    if (pos == std::string::npos) return "";
    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }
    auto end = json.find_first_of(",}]", pos);
    return json.substr(pos, end - pos);
}

/* Parse array of item objects from JSON body */
std::vector<OrderItem> parseItems(const std::string& json) {
    std::vector<OrderItem> items;
    auto arr_start = json.find("\"items\"");
    if (arr_start == std::string::npos) return items;
    auto brace = json.find('[', arr_start);
    if (brace == std::string::npos) return items;

    std::size_t pos = brace + 1;
    while (pos < json.size()) {
        auto obj_start = json.find('{', pos);
        if (obj_start == std::string::npos) break;
        auto obj_end = json.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);

        OrderItem item;
        item.id    = std::stoi(jsonGet(obj, "id").empty()    ? "0" : jsonGet(obj, "id"));
        item.qty   = std::stoi(jsonGet(obj, "qty").empty()   ? "0" : jsonGet(obj, "qty"));
        item.price = std::stod(jsonGet(obj, "price").empty() ? "0" : jsonGet(obj, "price"));
        item.name  = jsonGet(obj, "name");
        if (item.qty > 0 && item.price > 0) items.push_back(item);

        pos = obj_end + 1;
        if (pos < json.size() && json[pos] == ']') break;
    }
    return items;
}

// ═══════════════════════════════════════════════════════════
//  HTTP RESPONSE BUILDERS
// ═══════════════════════════════════════════════════════════
std::string httpResp(int code, const std::string& body,
                     const std::string& ct = "application/json") {
    std::string status = (code == 200) ? "200 OK" :
                         (code == 400) ? "400 Bad Request" : "500 Internal Server Error";
    std::ostringstream ss;
    ss << "HTTP/1.1 " << status << "\r\n"
       << "Content-Type: " << ct << "; charset=utf-8\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    return ss.str();
}

// ═══════════════════════════════════════════════════════════
//  ROUTE HANDLERS
// ═══════════════════════════════════════════════════════════

/* GET /ping — health check */
std::string handlePing() {
    return httpResp(200, R"({"status":"ok","server":"C++ Cafeteria Backend"})");
}

/* POST /calculate — compute bill totals */
std::string handleCalculate(const std::string& body) {
    auto items = parseItems(body);
    if (items.empty())
        return httpResp(400, R"({"error":"No valid items found"})");

    BillResult r = calculateBill(items);

    std::ostringstream j;
    j << std::fixed << std::setprecision(0);
    j << "{"
      << "\"itemCount\":"  << r.itemCount   << ","
      << "\"subtotal\":"   << r.subtotal    << ","
      << "\"tax\":"        << r.tax         << ","
      << "\"grandTotal\":" << r.grandTotal
      << "}";
    return httpResp(200, j.str());
}

/* POST /order — place an order, store it, return receipt */
std::string handleOrder(const std::string& body) {
    auto items    = parseItems(body);
    auto customer = jsonGet(body, "customer");
    if (customer.empty()) customer = "Walk-in Customer";
    if (items.empty())
        return httpResp(400, R"({"error":"No items in order"})");

    BillResult r = calculateBill(items);

    Order ord;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        ord.orderId   = g_nextId++;
        ord.customer  = customer;
        ord.items     = items;
        ord.subtotal  = r.subtotal;
        ord.tax       = r.tax;
        ord.grandTotal= r.grandTotal;
        ord.time      = currentTime();
        g_orders.push_back(ord);
    }

    /* Build JSON response */
    std::ostringstream j;
    j << std::fixed << std::setprecision(0);
    j << "{"
      << "\"orderId\":"   << ord.orderId      << ","
      << "\"customer\":"  << jsonStr(ord.customer) << ","
      << "\"time\":"      << jsonStr(ord.time) << ","
      << "\"subtotal\":"  << ord.subtotal      << ","
      << "\"tax\":"       << ord.tax           << ","
      << "\"grandTotal\":" << ord.grandTotal   << ","
      << "\"items\":[";
    for (std::size_t i = 0; i < ord.items.size(); ++i) {
        const auto& it = ord.items[i];
        j << "{"
          << "\"id\":"       << it.id           << ","
          << "\"name\":"     << jsonStr(it.name) << ","
          << "\"qty\":"      << it.qty           << ","
          << "\"price\":"    << it.price         << ","
          << "\"rowTotal\":" << it.rowTotal()
          << "}";
        if (i + 1 < ord.items.size()) j << ",";
    }
    j << "]}";
    return httpResp(200, j.str());
}

/* GET /summary — daily analytics calculated in C++ */
std::string handleSummary() {
    std::lock_guard<std::mutex> lk(g_mutex);

    if (g_orders.empty())
        return httpResp(200, R"({"totalOrders":0,"totalRevenue":0,"totalItems":0,"avgBill":0,"topItems":[],"orders":[]})");

    double totalRevenue = 0;
    int    totalItems   = 0;
    std::map<std::string, int> freq;

    for (const auto& o : g_orders) {
        totalRevenue += o.grandTotal;
        for (const auto& i : o.items) {
            totalItems      += i.qty;
            freq[i.name]    += i.qty;
        }
    }
    int avgBill = (int)std::round(totalRevenue / g_orders.size());

    /* Sort top items */
    std::vector<std::pair<std::string,int>> ranked(freq.begin(), freq.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b){ return b.second < a.second; });
    if (ranked.size() > 5) ranked.resize(5);

    std::ostringstream j;
    j << std::fixed << std::setprecision(0);
    j << "{"
      << "\"totalOrders\":"  << g_orders.size()   << ","
      << "\"totalRevenue\":" << totalRevenue        << ","
      << "\"totalItems\":"   << totalItems          << ","
      << "\"avgBill\":"      << avgBill             << ","
      << "\"topItems\":[";
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        j << "{\"name\":" << jsonStr(ranked[i].first)
          << ",\"qty\":"  << ranked[i].second << "}";
        if (i + 1 < ranked.size()) j << ",";
    }
    j << "],\"orders\":[";
    for (std::size_t i = 0; i < g_orders.size(); ++i) {
        const auto& o = g_orders[i];
        j << "{"
          << "\"orderId\":"   << o.orderId          << ","
          << "\"customer\":"  << jsonStr(o.customer) << ","
          << "\"time\":"      << jsonStr(o.time)     << ","
          << "\"grandTotal\":" << o.grandTotal        << ","
          << "\"items\":[";
        for (std::size_t k = 0; k < o.items.size(); ++k) {
            const auto& it = o.items[k];
            j << "{\"name\":" << jsonStr(it.name)
              << ",\"qty\":"  << it.qty << "}";
            if (k + 1 < o.items.size()) j << ",";
        }
        j << "]}";
        if (i + 1 < g_orders.size()) j << ",";
    }
    j << "]}";
    return httpResp(200, j.str());
}

// ═══════════════════════════════════════════════════════════
//  REQUEST DISPATCHER
// ═══════════════════════════════════════════════════════════
std::string dispatch(const std::string& req) {
    /* Extract method and path */
    std::istringstream ss(req);
    std::string method, path;
    ss >> method >> path;

    /* Extract body (after blank line) */
    std::string body;
    auto sep = req.find("\r\n\r\n");
    if (sep != std::string::npos) body = req.substr(sep + 4);

    /* CORS preflight */
    if (method == "OPTIONS")
        return httpResp(200, "");

    if (method == "GET"  && path == "/ping")    return handlePing();
    if (method == "GET"  && path == "/summary") return handleSummary();
    if (method == "POST" && path == "/calculate") return handleCalculate(body);
    if (method == "POST" && path == "/order")     return handleOrder(body);

    return httpResp(400, R"({"error":"Unknown route"})");
}

// ═══════════════════════════════════════════════════════════
//  CONNECTION HANDLER  (one thread per client)
// ═══════════════════════════════════════════════════════════
void handleClient(SOCKET client) {
    char buf[8192] = {};
    int  received  = recv(client, buf, sizeof(buf) - 1, 0);
    if (received > 0) {
        std::string request(buf, received);
        std::string response = dispatch(request);
        send(client, response.c_str(), (int)response.size(), 0);
    }
    closesocket(client);
}

// ═══════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════
int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n"; return 1;
    }
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket() failed\n"; return 1;
    }

    /* Allow port reuse */
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    const char* port_env = std::getenv("PORT");
    int PORT = port_env ? std::stoi(port_env) : 8080;
    addr.sin_port = htons(PORT);

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed — is port " << PORT << " already in use?\n";
        closesocket(server); return 1;
    }

    if (listen(server, BACKLOG) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n"; closesocket(server); return 1;
    }

    std::cout << "\n";
    std::cout << "  ╔══════════════════════════════════════╗\n";
    std::cout << "  ║   University Cafeteria C++ Backend   ║\n";
    std::cout << "  ║   Listening on port " << PORT << "\n";
    std::cout << "  ╠══════════════════════════════════════╣\n";
    std::cout << "  ║  Routes:                             ║\n";
    std::cout << "  ║    GET  /ping       health check     ║\n";
    std::cout << "  ║    POST /calculate  bill totals      ║\n";
    std::cout << "  ║    POST /order      place order      ║\n";
    std::cout << "  ║    GET  /summary    daily analytics  ║\n";
    std::cout << "  ╚══════════════════════════════════════╝\n\n";
    std::cout << "  Waiting for connections…\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        SOCKET client = accept(server, (sockaddr*)&client_addr, &client_len);
        if (client == INVALID_SOCKET) continue;

        /* Spawn a thread per connection */
        std::thread(handleClient, client).detach();
    }

#ifdef _WIN32
    WSACleanup();
#endif
    closesocket(server);
    return 0;
}