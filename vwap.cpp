#include "vwap.h"

namespace po = boost::program_options;

void processITCH(const char* msg, uint16_t msgLen, char msgType, uint64_t timestamp)
{
    switch (msgType) {
        case 'P':
            TradeMessage(msg, msgLen, timestamp);
            break;
        case 'C':
            ExecutedPriceOrderMessage(msg, msgLen, timestamp);
            break;
        case 'E':
            ExecutedOrderMessage(msg, msgLen, timestamp);
            break;
        case 'A':
            AddOrderMessage(msg, msgLen, msgType);
            break;
        case 'F':
            AddOrderMessage(msg, msgLen, msgType);
            break;
        case 'D':
            DeleteOrderMessage(msg, msgLen);
            break;
        case 'Q':
            CrossTradeMessage(msg, msgLen, timestamp);
            break;
        case 'B':
            BrokenTradeMessage(msg, msgLen);
            break;
        case 'U':
            ReplaceOrderMessage(msg, msgLen);
            break;
        default:
            // Any other message type which is not needed for VWAP calculation can be ignored
            break;
    }
}


// Print the cumulative VWAP for each stock at a given hour to a file in outputDir
void printVWAP(const string& outputDir, uint64_t hour,
               const unordered_map<string, TradeAggregate>& cumulative) {

    string filename = outputDir + "/" + printTime(hour) + ".txt";
    ofstream outFile(filename);
    if (!outFile) {
        cout << "Error: Unable to open output file: " << filename << endl;
        return;
    }
    outFile << "VWAP at time: " << printTime(hour) << "\n";
    outFile << "Symbol  VWAP\n";
    outFile << "--------------\n";

    for (const auto& entry : cumulative) {
        const string& stock = entry.first;
        const TradeAggregate& agg = entry.second;
        if (agg.volume > 0) {

            // Nasdaq ITCH stores price as integer in 1/10000 dollars
            double vwap = static_cast<double>(agg.priceVolume) / (agg.volume * 10000.0);
            outFile << stock << "   " << fixed << setprecision(4) << vwap << "\n";
        }
    }
    outFile.close();
}


int main(int argc, char** argv) {

    // Parse command-line options
    po::options_description desc("Command line options");
    desc.add_options()
            ("help,h", "produce help message")
            ("input,i", po::value<string>(), "input uncompressed ITCH file")
            ("output,o", po::value<string>(), "output directory");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help") || !vm.count("input") || !vm.count("output")) {
        cout << desc << "\n";
        return 1;
    }

    string inputFile = vm["input"].as<string>();
    string outputDir = vm["output"].as<string>();

    // Open the uncompressed ITCH file in binary mode
    ifstream ifs(inputFile, ios::binary);
    if (!ifs) {
        cout << "Error: Could not open input file: " << inputFile << "\n";
        return 1;
    }

    // firstMsg
    bool iffirstMsg = true;
    uint64_t currentHour = 0;

    cout << "Begin processing" <<  "\n";

    // Main processing loop: read and process one message at a time.
    while (true) {

        // read message length and detect EOF
        char lengthBuf[2];
        if (!ifs.read(lengthBuf, 2)) break;

        uint16_t msgLen = read_2bytes(lengthBuf);
        if (msgLen == 0) break;

        vector<char> msg(msgLen);

        if (!ifs.read(msg.data(), msgLen)) break;

        char msgType = msg[0];

        // Extract 6-byte timestamp at offset 5
        uint64_t timestamp = 0;
        if (msgLen >= 11) {
            timestamp = read_nbytes(msg.data() + 5, 6);
        }

        // 1 hour = 3600000000000 nanoseconds
        uint64_t msgHour = timestamp / 3600000000000ULL;
        if (iffirstMsg) {
            cout << "Hour checkpoint " <<  currentHour << "\n";
            currentHour = msgHour;
            iffirstMsg = false;
        }

        //If message's hour casted as int is greater than cur output vwap
        while (msgHour > currentHour) {
            cout << "Hour checkpoint " <<  msgHour << " begins" << "\n";
            printVWAP(outputDir, currentHour, VWAPaggregator);
            ++currentHour;
        }

        processITCH(msg.data(), msgLen, msgType, timestamp);
    }

    // Output the VWAP for the final hour
    printVWAP(outputDir, currentHour, VWAPaggregator);

    cout << "Processing complete. Output files are in directory : " << outputDir << "\n";


    return 0;
}


