

## Overview

The application reads an uncompressed ITCH file, and calculates the VWAP for each traded stock every hour. The VWAP is calculated as follows:

`VWAP = (Sum(Price * Volume)) / (Sum(Volume))`

The specification required the VWAP to be outputted every hour. Thus, I outputted the cumulative VWAP for each stock, until that hour, for every trading hour. 
For example 10:00.txt will output the VWAP for all trades that occurred upto but not including 10:00.  

In this submission I included an output directory where I store the hourly text outputs from a prior run. 


## Usage

This program requires the boost library as it uses the boost program options and algorithms library.

Build and run the program from the command line via the following commands:
```bash
clang++ -std=c++17 vwap.cpp -o Treqquant_TakeHome -lboost_program_options

./Treqquant_TakeHome --input path/to/itch_file --output path/to/output_directory
```
Use the --help param to see the required command line parameters. If running on ARM architecture, ```-arch arm64``` may need to be added to 
the build command. 

A CMakeLists.txt is also included which can be used to generate build files by running the command:
```bash
cmake CMakeLists.txt
```


## Runtime of the VWAP Aggregator
The program reads the ITCH file sequentially, processing one message at a time. For each message, it parses the binary data, extracts fields of interests,
and updates in-memory data structures like activeOrders, executedTrades, tradeMatchIDMap, and VWAPaggregator.
Since these operations for each message are performed in constant or near-constant time, the overall processing time is **O(n)** where **n** is the number of messages in the ITCH file.


## Analysis of Output
Before 9:00 AM, my program correctly outputs a small number of stocks in the text files as there is minimal pre-trade activity. 
After 9:00 AM, the number of stocks outputted drastically increases to the thousands and slightly increases throughout the day
as more equities are traded. The program correctly ends at 20:00 as equities are generally finished trading at 20:00. 

To verify the accuracy of the VWAP output, I cross-referenced the calculated VWAP values with the 
stock prices for January 30, 2019 for some stocks, later in the trading day. For all the stocks I cross-referenced, the 
VWAP was very close to the actual stock price, indicating that the program logic for VWAP calculation is correct. 