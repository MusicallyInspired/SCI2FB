/********************************************************************
*   SCI2FB conversion utility   v1.00                               *
*   by Brandon Blume                                                *
*   shine62@gmail.com                                               *
*   March XX, 2023                                                  *
*                                                                   *
*   Command line tool to convert a FB-01 Sierra SCI0 Patch resource *
*   into two FB-01 sysex Bank files.                                *
*                                                                   *
*   You're free to do with it as you please. This program could     *
*   probably be vastly improved to be more efficient, but it works. *
********************************************************************/

#include <fstream>
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstring>

using namespace std;

float nVersion = 1.00;

void read_file(ifstream& file, vector<char>& data, std::streamoff titleOffset);
void nibblize_data(vector<char>& data, vector<char>& splitData1, vector<char>& splitData2);
void write_to_file(std::vector<char> splitData1, std::vector<char> splitData2, const char* output_filename1, const char* output_filename2);
bool check_file_exists(const char* filename);
void check_output_file(string output_filename);

int main(int argc, char* argv[]) {
    // Check if the user provided exactly three arguments

    std::cout << std::fixed;
    std::cout << std::setprecision(2);
    cout << "\nSCI2FB  v" << nVersion << "    by Brandon Blume    March XX, 2023" << endl;

    if (argc != 4) {
        cout << "   usage:  " << argv[0] << "   patfile   bankfile1   bankfile2\n";
        return 1;
    }
    cout << endl;

    // Get the filenames from the command line arguments
    char* input_filename = argv[1];
    char* output_filename1 = argv[2];
    char* output_filename2 = argv[3];

    // Check if patfile exists
    if (!check_file_exists(input_filename)) {
        cout << "Error: file " << input_filename << " not found" << endl;
        exit(EXIT_FAILURE);
    }

    // Open patfile
    ifstream input_file(input_filename, ios::binary);
    input_file.exceptions(std::ios::failbit | std::ios::badbit);

    // Check if the SCI patch resource identifier header exists
    input_file.seekg(0x00);
    char buffer[2];
    input_file.read(buffer, 1);
    if (buffer[0] == (char)0x89) {
        cout << "SCI patch resource header detected" << endl;
    }
    else {
        cout << "Error: input file is not a valid SCI patch resource." << endl;
        exit(EXIT_FAILURE);
    }

    // Check for title string length in second byte of header to use as offset for future file handling
    input_file.seekg(0x01);
    char titleStringSize[1];
    input_file.read(titleStringSize, 1);
    std::streamoff titleOffset = static_cast<std::streamoff>(static_cast<int>(titleStringSize[0]));

    // Check size of file to ensure it's valid
    input_file.seekg(0, ios::end);
    std::streamoff length = input_file.tellg();
    input_file.seekg(0, ios::beg);

    if (length != 6148 + titleOffset) {
        cout << input_filename << " is not the expected size (6148 bytes + title string length). Not a valid FB-01 SCI0 Patch file." 
            << endl << "Actual size: " << length << endl << "Title string length: " << static_cast<int>(titleStringSize[0]) << endl;
        exit(EXIT_FAILURE);
    }

    // Ensure the ABCDh bytes exist at address 0xC02
    // (offset by the length of the title string defined in the header which we stored earlier)
    input_file.seekg(0xC02 + titleOffset);
    input_file.read(buffer, 2);
    if (buffer[0] == (char)0xAB && buffer[1] == (char)0xCD) {
        cout << "Bank separator bytes found. Input patch file is valid" << endl;
    }
    else {
        cout << "Error: input file is not a valid FB-01 SCI patch resource." << endl;
        exit(EXIT_FAILURE);
    }


    // Check if output bank files 1 and 2 already exist. If they do, ask user whether to overwrite or abort
    check_output_file(output_filename1);
    check_output_file(output_filename2);

    
    //
    // Finished file error handling, continue with the actual operation
    //
        
    // Read the input patch file into memory
    vector<char> data;
    read_file(input_file, data, titleOffset);

    // Close the input file
    input_file.close();

    
    // Split the bytes of each instrument voice packet in order of: low nibble = high byte, high nibble = low byte
    vector<char> splitData1;
    vector<char> splitData2;
    splitData1.reserve(data.size());
    splitData2.reserve(data.size());
    splitData1.clear();
    splitData2.clear();
    nibblize_data(data, splitData1, splitData2);
    
    // Create the sysex bank files with the new "nibblized" data
    write_to_file(splitData1, splitData2, output_filename1, output_filename2);
    
    cout << "FB-01 sysex banks created successfully!" << endl;

    return 0;
}

void read_file(ifstream& file, vector<char>& data, std::streamoff titleOffset) {
    // Read the file starting at the first voice data byte after the header bytes. Iterate through each
    // byte for each of the 96 voices. (we must skip the separator bytes ABCDh on voice 49, which is
    //      voice 1 of bank B)
    streampos pos = 0x02 + titleOffset;

    for (int i = 0; i < 96; i++) {
        // If we're on the 49th voice we're in bank B and need to skip the ABCDh seperator bytes first
        if (i == 48) pos += 2;
        // Store the instrument voice data into "data"
        file.seekg(pos);
        char buffer[64];
        file.read(buffer, 64);
        data.insert(data.end(), buffer, buffer + 64);
        pos += 64;
    }
}

void nibblize_data(vector<char>& data, vector<char>& splitData1, vector<char>& splitData2) {
    // Check to ensure that both sets of instrument voice packets equal 6144 bytes in length (64 bytes per 96 voices form the patch file).
    if (data.size() != 6144) {
        cout << "Error: data vector not the expected size (6144)" << endl;
        cout << "Actual size = " << data.size() << endl;
        exit(EXIT_FAILURE);
    }
    
    //////////////////////////////////////////////////////////////////////////////////////////////
    //  Now we must nibblize the voice patch data by splitting each byte into pairs and         //
    //  and storing them in order: low nibble = high byte, high nibble = low byte's low nibble. //
    //  This will prepare the voice data properly for the sysex format and double the size of   //
    //  each voice's byte data adding leading bytes (0x01 0x00) to signify the size of the      //
    //  packet (128) and a checksum byte calculate with 2's complement of sum following it.     //
    //  (131 bytes total per instrument voice)                                                  //
    //////////////////////////////////////////////////////////////////////////////////////////////

    // First 48 instrument voice packets (bank A)
    for (int i = 0; i < 48; i++) {
        
        // set the high-byted packet size value bytes that precede the packet (0x01 0x00 = 128)
        splitData1.push_back(0x01);
        splitData1.push_back(0x00);
        
        // 64 bytes per packet
        for (int j = 0; j < 64; j++) {
            int index = i * 64 + j;
            splitData1.push_back(data[index] & 0x0F);
            splitData1.push_back((data[index] >> 4) & 0x0F);
        }
        
        // calculate checksum with 2's complement of sum
        unsigned char checksum = 0;
        for (int j = 2; j < 130; j++) {
            checksum += static_cast<unsigned int>(splitData1[i * 131 + j]);
        }
        checksum = ((~(checksum & 0xFF)) + 1) & 0x7F;
        splitData1.push_back(checksum);
    }

    // Second 48 instrument voice packets (bank B)
    for (int i = 0; i < 48; i++) {
        // set packet size of nibblized bytes preceding the packet:
        //          0x01 0x00 = 128 (%0000000h, %0lllllll -> %00000000, %hlllllll)
        splitData2.push_back(0x01);
        splitData2.push_back(0x00);

        // 64 bytes per packet
        for (int j = 0; j < 64; j++) {
            int index = (48 + i) * 64 + j;
            splitData2.push_back(data[index] & 0x0F);
            splitData2.push_back((data[index] >> 4) & 0x0F);
        }

        // calculate checksum with 2's complement of sum
        unsigned char checksum = 0;
        for (int j = 2; j < 130; j++) {
            checksum += static_cast<unsigned int>(splitData2[i * 131 + j]);
        }
        checksum = ((~(checksum & 0xFF)) + 1) & 0x7F;
        splitData2.push_back(checksum);
    }
}

void write_to_file(std::vector<char> splitData1, std::vector<char> splitData2, const char* output_filename1, const char* output_filename2) {
    
    // Open the output file in binary mode for writing
    std::ofstream out_file1(output_filename1, std::ios::binary);
    std::ofstream out_file2(output_filename2, std::ios::binary);
    
    //////////////////////////////////////////////////////////////////////////////////////////
    //  The format of the FB-01 bank sysex files we must create is structured like so:      //
    //                                                                                      //
    //  For bank A:                                                                         //
    //  $00-                                                                                //
    //   $06:   F0 43 75 00 00 00 00h.......FB-01's "send bank A" sysex code                //
    //--------------------------------------------------------------------------------------//
    //  For bank B:                                                                         //
    //  $00-                                                                                //
    //   $06:   F0 43 75 00 00 00 01h.......FB-01's "send bank B" sysex code                //
    //--------------------------------------------------------------------------------------//
    //  $07-                                                                                //
    //   $08:   00 40h......................Bank info packet size (64)                      //
    //  $19-                                                                                //
    //   $48:   <bank description>..........8-byte string for name + reserved empty bytes   //
    //  $49 :   Checksum....................2's complement of sum                           //
    //  $4A-                                                                                //
    //   $4B:   01 00h......................Bank voice #1 packet size (128)                 //         
    //  $4C-                                                                                //
    //   $CB:   <patch data>................Voice #1 patch data                             //
    //  $CC :   Checksum....................2's complement of sum                           //
    //    "          "                           "                                          //
    //    "          "                           "                                          //
    //    "     ....."......................Voice #48                                       // 
    // $18DA:   F7h.........................End sysex                                       //
    //                                                                                      //
    //  The resulting files will each be exactly 6363 bytes long.                           //
    //////////////////////////////////////////////////////////////////////////////////////////
    
    // Prepare an array for the whole header for the bank 1 output file
    char bank1header[74] = { '\xF0', '\x43', '\x75', '\x00', '\x00', '\x00', '\x00', '\x00', '\x40', '\0' };
    
    // Prepare bank 1 info packet, pulling the name of the bank from the first output filename given.
    // The first 7 characters of outfile1 are pulled from the filename. If it is less than 7 characters,
    // it fills the remaining spaces with 0x20 (space character) and the 8th character with a '1' (bank 1).

    char bank1InfoPacket[32] = { 0 };
    char* bank1name = bank1InfoPacket;

    int len = strlen(output_filename1);
    strncpy(bank1name, output_filename1, 7); // Copy up to 7 characters from output_filename1 into bank1name
    // If the length of output_filename1 is less than 7, fill the remaining characters with spaces
    if (len < 7) {
        memset(bank1name + len, 0x20, 7 - len);
    }
    bank1name[7] = '1'; // Set the 8th character to '1' (bank 1)

    // Now nibblize the packet which will double its length and be stored in bank1header
    for (int i = 0; i < sizeof(bank1InfoPacket); i++) {
        unsigned char high_nibble = (bank1InfoPacket[i] >> 4) & 0x0F; // extract high nibble
        unsigned char low_nibble = bank1InfoPacket[i] & 0x0F; // extract low nibble

        bank1header[9 + i * 2] = low_nibble; // write low nibble to even-indexed output array
        bank1header[9 + i * 2 + 1] = high_nibble; // write high nibble to odd-indexed output array
    }
    // One final step, calculate the packet's checksum and store it at the end of the header in the last index
    unsigned char checksum = 0;
    for (int i = 0; i < 64; i++) {
        checksum += static_cast<unsigned int>(bank1header[9 + i]); // Sum all the bytes in the packet together
    }
    checksum = ((~(checksum & 0xFF)) + 1) & 0x7F; // Drop all but the lowest 8 bits, flip the bits, add 1, then mask the lowest 7 bits for the correct checksum value
    bank1header[73] = checksum;
    
    //
    // Now for bank 2's header and info packet
    char bank2header[74] = { '\xF0', '\x43', '\x75', '\x00', '\x00', '\x00', '\x01', '\x00', '\x40', '\0' };
    char bank2InfoPacket[32] = { 0 };
    char* bank2name = bank2InfoPacket;

    len = strlen(output_filename2);
    strncpy(bank2name, output_filename2, 7); // Copy up to 7 characters from output_filename2 into bank2name
    // If the length of output_filename2 is less than 7, fill the remaining characters with spaces
    if (len < 7) {
        memset(bank2name + len, 0x20, 7 - len);
    }
    bank2name[7] = '2'; // Set the 8th character to '2' (bank 2)

    // Nibblize bank 2's info packet
    for (int i = 0; i < sizeof(bank2InfoPacket); i++) {
        unsigned char high_nibble = (bank2InfoPacket[i] >> 4) & 0x0F;
        unsigned char low_nibble = bank2InfoPacket[i] & 0x0F;

        bank2header[9 + i * 2] = low_nibble;
        bank2header[9 + i * 2 + 1] = high_nibble;
    }
    // Calculate and store bank 2's checksum
    checksum = 0;
    for (int i = 0; i < 64; i++) {
        checksum += static_cast<unsigned int>(bank2header[9 + i]);
    }
    checksum = ((~(checksum & 0xFF)) + 1) & 0x7F;
    bank2header[73] = checksum;

    // Begin writing bank A sysex file
    out_file1.seekp(0, std::ios::beg);
    out_file1.write(reinterpret_cast<char*>(&bank1header), sizeof(bank1header)); // Write the header to outfile1
    out_file1.write(splitData1.data(), splitData1.size()); // Write the already nibblized and checksummed voice data packets
    out_file1.write("\xF7", 1); // Write the final closing byte to end the exclusive message

    // Begin writing bank B sysex file
    out_file2.seekp(0, std::ios::beg);
    out_file2.write(reinterpret_cast<char*>(&bank2header), sizeof(bank2header));
    out_file2.write(splitData2.data(), splitData2.size());
    out_file2.write("\xF7", 1);

    // Close both files
    out_file1.close();
    out_file2.close();
}

bool check_file_exists(const char* filename) {
    std::ifstream infile(filename);
    return infile.good();
}

void check_output_file(string output_filename) {
    ifstream file(output_filename);
    if (file.good()) {
        cout << "\nOutput file already exists. Do you want to overwrite it? (Y/N): ";
        string answer;
        cin >> answer;
        if (answer == "Y" || answer == "y") {
            ofstream file(output_filename, ios::trunc);
            cout << "File " << output_filename << " successfully wiped.\n" << endl;
            file.close();
        }
        else {
            cout << "Aborting operation..." << endl;
            exit(EXIT_FAILURE);
        }
    }
}
