#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <string>
#include <zlib.h>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string/trim.hpp>

using namespace std;


/**
// Variable and Struct Declarations
 */

// Holds data about a single order as it was added to the order book
struct OrderData {
    std::string stock;
    uint32_t price;
    char side;    // buy or sell
    uint32_t shares;
};


// Record of a trade
struct TradeRecord {
    char msgType;
    uint64_t matchID;
    std::string stock;
    uint32_t price;
    uint32_t volume;
    uint64_t timestamp;
};


// Represents order book where active orders are added
static unordered_map<uint64_t, OrderData> activeOrders;


/**
 * Maps a stock symbol to a vector of TradeRecord entries
 * Records each executed trade event for the stock
 */
static unordered_map<std::string, std::vector<TradeRecord>> executedTrades;


/**
 * Maps each unique trade’s matchID to its TradeRecord object
 * Used primiarly to find and resolve broken trades
 */
static unordered_map<uint64_t, TradeRecord> tradeMatchIDMap;


/**
 * Stores cumulative trading data for a given stock for VWAP calculations
 * VWAP = (priceVolume / volume) / 10000.0
 */
struct TradeAggregate {
    uint64_t volume = 0;
    uint64_t priceVolume = 0;
};


/**
* Global aggregator to accumulate trade data for quick VWAP calculations.
* Maps stock symbol to a TradeAggregate struct
*/
static unordered_map<std::string, TradeAggregate> VWAPaggregator;


/**
// Helper Functions
 */


// Reads 2 bytes from input pointer and returns a 16-bit unsigned int
inline uint16_t read_2bytes(const char* p) {
    return ( (static_cast<uint16_t>(static_cast<unsigned char>(p[0])) << 8) |
             static_cast<uint16_t>(static_cast<unsigned char>(p[1])) );
}

// Reads 4 bytes from input pointer and returns a 32-bit unsigned int
inline uint32_t read_4bytes(const char* p) {
    return ( (static_cast<uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
             (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
             (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 8)  |
             (static_cast<uint32_t>(static_cast<unsigned char>(p[3]))) );
}


// Reads variable length from input pointer and returns a 64-bit unsigned int
// used for either 8 or 6 byte reads
inline uint64_t read_nbytes(const char* p, int len) {
    uint64_t value = 0;
    for (int i = 0; i < len; i++) {
        value = (value << 8) | static_cast<unsigned char>(p[i]);
    }
    return value;
}


// convert a numeric hour value into a formatted string for text file output
std::string printTime(uint64_t hour) {
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02llu:00", hour % 24);
    return std::string(buffer);
}


// A or F messagetype
void AddOrderMessage(const char* msg, uint16_t msgLen, char msgType) {

    // Check minimum length depending on message type
    if (msgType == 'A') {
        if (msgLen < 36) return;
    } else if (msgType == 'F') {
        if (msgLen < 40) return;
    } else {
        return;
    }


    uint64_t orderRef = read_nbytes(msg + 11, 8);    // Order ref (8 bytes starting at offset 11)

    char side = msg[19];                                    // Buy/Sell indicator (1 byte at offset 19)

    uint32_t numShares = read_4bytes(msg + 20);           // Number of shares (4 bytes at offset 20)

    // Stock Symbol (8 bytes at offset 24)
    char stockBuf[9];
    std::memcpy(stockBuf, msg + 24, 8);
    stockBuf[8] = '\0';
    std::string stockSym(stockBuf);
    boost::algorithm::trim(stockSym);

    uint32_t price = read_4bytes(msg + 32);             // Price (4 bytes at offset 32)

    // Parse and ignore the MPID field for "F" messages
    if (msgType == 'F') {
        char mpidBuf[5];
        std::memcpy(mpidBuf, msg + 36, 4);
        mpidBuf[4] = '\0';
        std::string mpid(mpidBuf);
    }

    OrderData od;
    od.stock  = stockSym;
    od.price  = price;
    od.side   = side;
    od.shares = numShares;

    activeOrders[orderRef] = od;
}



// U messagetype
void ReplaceOrderMessage(const char* msg, uint16_t msgLen)
{
    if (msgLen < 35) return;

    uint64_t oldID = read_nbytes(msg + 11, 8);
    uint64_t newID = read_nbytes(msg + 19, 8);

    uint32_t newPrice = read_4bytes(msg + 31);

    // Move old order data to new ref and update price
    auto it = activeOrders.find(oldID);
    if (it != activeOrders.end()) {
        OrderData od = it->second;
        od.price = newPrice;
        activeOrders.erase(it);
        activeOrders[newID] = od;
    }
}


// D messagetype
void DeleteOrderMessage(const char* msg, uint16_t msgLen)
{
    if (msgLen < 19) return;

    uint64_t orderRef = read_nbytes(msg + 11, 8);
    activeOrders.erase(orderRef);
}


// P messagetype
void TradeMessage(const char* msg, uint16_t msgLen, uint64_t timestamp)
{
    if (msgLen < 44) return;

    uint64_t orderRef = read_nbytes(msg + 11, 8);           // order reference (8 bytes at offset 11)

    char side = msg[19];
    uint32_t numShares = read_4bytes(msg + 20);                 // num shares (4 bytes at offset 20)

    char stockBuf[9];
    std::memcpy(stockBuf, msg + 24, 8);                 // stock name (8 bytes at offset 24)
    stockBuf[8] = '\0';
    std::string stockSym(stockBuf);
    boost::algorithm::trim(stockSym);

    uint32_t price  = read_4bytes(msg + 32);                    // price (4 bytes at offset 32)
    uint64_t tradeMatchNum  = read_nbytes(msg + 36, 8);     // matching number (8 bytes at offset 32)

    // Update the VWAP aggregator for the target stock
    TradeAggregate &agg = VWAPaggregator[stockSym];
    agg.volume += numShares;
    agg.priceVolume += static_cast<uint64_t>(numShares) * price;


    // Record the trade
    TradeRecord tr;
    tr.msgType    = 'P';
    tr.matchID = tradeMatchNum;
    tr.stock      = stockSym;
    tr.price      = price;
    tr.volume     = numShares;
    tr.timestamp  = timestamp;
    executedTrades[stockSym].push_back(tr);

    // Track matching number for broken trades
    tradeMatchIDMap[tradeMatchNum] = tr;
}


// E messagetype
void ExecutedOrderMessage(const char* msg, uint16_t msgLen, uint64_t timestamp)
{
    if (msgLen < 31) return;

    uint64_t orderRef = read_nbytes(msg + 11, 8);           // order reference (8 bytes at offset 11)
    uint32_t numShares = read_4bytes(msg + 19);               // number of shares (4 bytes at offset 19)
    uint64_t tradeMatchNum = read_nbytes(msg + 23, 8);      // match num (8 bytes at offset 23)

    auto it = activeOrders.find(orderRef);
    if (it != activeOrders.end()) {
        OrderData &od = it->second;

        uint32_t price = od.price;

        // Update the VWAP aggregator for the target stock
        TradeAggregate &agg = VWAPaggregator[od.stock];
        agg.volume += numShares;
        agg.priceVolume += static_cast<uint64_t>(numShares) * price;

        // Record the trade
        TradeRecord tr;
        tr.msgType    = 'E';
        tr.matchID = tradeMatchNum;
        tr.stock      = od.stock;
        tr.price      = price;
        tr.volume     = numShares;
        tr.timestamp  = timestamp;
        executedTrades[od.stock].push_back(tr);

        // Track matching number for broken trades
        tradeMatchIDMap[tradeMatchNum] = tr;
    }
}


// C messagetype
void ExecutedPriceOrderMessage(const char* msg, uint16_t msgLen, uint64_t timestamp)
{
    if (msgLen < 36) return;

    uint64_t orderRef = read_nbytes(msg + 11, 8);          // order reference (8 bytes at offset 11)
    uint32_t numShares = read_4bytes(msg + 19);                 // number of shares (4 bytes at offset 19)
    uint64_t tradeMatchNum = read_nbytes(msg + 23, 8);     // match num (8 bytes at offset 23)
    uint32_t executionPrice = read_4bytes(msg + 32);            // execution price (4 bytes at offset 32)

    auto it = activeOrders.find(orderRef);
    if (it != activeOrders.end()) {
        OrderData &od = it->second;

        // Update the VWAP aggregator for the target stock
        TradeAggregate &agg = VWAPaggregator[od.stock];
        agg.volume += numShares;
        agg.priceVolume += static_cast<uint64_t>(numShares) * executionPrice;

        // Record the trade
        TradeRecord tr;
        tr.msgType    = 'C';
        tr.matchID = tradeMatchNum;
        tr.stock      = od.stock;
        tr.price      = executionPrice;
        tr.volume     = numShares;
        tr.timestamp  = timestamp;
        executedTrades[od.stock].push_back(tr);

        // Track matching number for broken trades
        tradeMatchIDMap[tradeMatchNum] = tr;
    }
}


// Q messagetype
void CrossTradeMessage(const char* msg, uint16_t msgLen, uint64_t timestamp)
{
    if (msgLen < 39) return;

    uint64_t shares = read_nbytes(msg + 11, 8);  // Number of shares (8 bytes at offset 11)
    char stockBuf[9];
    std::memcpy(stockBuf, msg + 19, 8);  // Stock symbol (8 bytes at offset 19)
    stockBuf[8] = '\0';
    std::string stockSym(stockBuf);
    boost::algorithm::trim(stockSym);

    uint32_t crossPrice = read_4bytes(msg + 27);  // Cross price (4 bytes at offset 27)
    uint64_t tradeMatchNum = read_nbytes(msg + 31, 8);  // Match number (8 bytes at offset 31)

    if (shares > 0) {

        //Update the aggregator for VWAP calculations
        TradeAggregate &agg = VWAPaggregator[stockSym];
        agg.volume += static_cast<uint32_t>(shares);
        agg.priceVolume += static_cast<uint64_t>(shares) * crossPrice;

        // Record the trade
        TradeRecord tr;
        tr.msgType    = 'Q';
        tr.matchID = tradeMatchNum;
        tr.stock      = stockSym;
        tr.price      = crossPrice;
        tr.volume     = static_cast<uint32_t>(shares);
        tr.timestamp  = timestamp;
        executedTrades[stockSym].push_back(tr);

        // Track matching number for broken trades
        tradeMatchIDMap[tradeMatchNum] = tr;
    }
}


// B messagetype
void BrokenTradeMessage(const char* msg, uint16_t msgLen)
{
    if (msgLen < 19) return;
    uint64_t tradeMatchNum = read_nbytes(msg + 11, 8); // Match number (8 bytes at offset 11)

    auto it = tradeMatchIDMap.find(tradeMatchNum);
    if (it != tradeMatchIDMap.end()) {

        //Find and remove that trade from orderBook
        const TradeRecord &tr = it->second;
        const std::string &stockSym = tr.stock;

        auto &v = executedTrades[stockSym];
        for (auto vt = v.begin(); vt != v.end(); ++vt) {
            if (vt->matchID == tradeMatchNum && vt->msgType == tr.msgType) {
                v.erase(vt);
                break;
            }
        }
        tradeMatchIDMap.erase(it);
    }
}