/************************************************************************
*   SCI2FB conversion utility   v1.01                                   *
*   by Brandon Blume                                                    *
*   shine62@gmail.com                                                   *
*   March 4, 2023                                                       *
*                                                                       *
*   Command line tool to convert an FB-01 Sierra SCI0 Patch resource    *
*   into one or two sysex bank files depending on how many are          *
*   stored in the Patch file.                                           *
*                                                                       *
*   You're free to do with it as you please. This program could         *
*   probably be vastly improved to be more efficient, but it works.     *
************************************************************************/

#include <fstream>
#include <iostream>
#include <vector>
#include <iomanip>
//#include <cstring>

using namespace std;

float nVersion = 1.01;

void read_file(ifstream& file, vector<char>& data, streamoff titleOffset, int nVoices);
void nibblize_data(vector<char>& data, vector<char>& splitData1, vector<char>* splitData2 = nullptr);
void write_to_file(vector<char> splitData1, const char* output_bank1, vector<char>* splitData2 = nullptr, const char* output_bank2 = nullptr);
bool check_file_exists(const char* filename);
void overwrite_check(string output_filename);

int main(int argc, char* argv[]) {
    // Check if the user provided arguments
    
    cout << fixed;
    cout << setprecision(2);
    cout << "\nSCI2FB  v" << nVersion << "    by Brandon Blume" << endl;

    if (argc != 2 && argc != 3) {
        cout << "   usage:   " << argv[0] << "   patfile  [output_bank]\n";
        return 1;
    }
    cout << "---------------------------------" << endl;

    // Get the patfile filename from command line arguments
    char patfile_name[256];
    strcpy(patfile_name, argv[1]);
    
    if (!strrchr(patfile_name, '.')) {
        // Input patfile does not contain a file extension.

        if (!check_file_exists(patfile_name)) { // If patfile without an extension doesn't exist
            strcat(patfile_name, ".pat");
            if (!check_file_exists(patfile_name)) { // If patfile with ".pat" extension doesn't exist
                strcpy(patfile_name + strlen(patfile_name) - 3, "002");
                if (!check_file_exists(patfile_name)) { // If patfile with ".002" extension doesn't exist
                    // Out of options, give up
                    patfile_name[strlen(patfile_name) - 4] = '\0';
                    cout << "Error: file " << patfile_name << " not found" << endl;
                    return 1;
                }
            }
        }
    }
    else {
        // Check if patfile with a user-inputted file extension exists
        if (!check_file_exists(patfile_name)) {
            cout << "Error: file " << patfile_name << " not found" << endl;
            return 1;
        }
    }

    // If given, get output_bank filename from the command line arguments
    // If output_bank was not specified, pull the name from patfile instead
    char output_bank[256];
    if (argc == 2) strcpy(output_bank, patfile_name);
    if (argc == 3) strcpy(output_bank, argv[2]);
    // Drop any extension given (we'll make out own later)
    if (char* ext_pos = strrchr(output_bank, '.')) *ext_pos = '\0';
    
    
    // Open patfile
    ifstream patfile(patfile_name, ios::binary);
    patfile.exceptions(ios::failbit | ios::badbit);
    // Check if the SCI patch resource identifier header exists
    patfile.seekg(0x00);
    char buffer[2];
    patfile.read(buffer, 1);
    if (buffer[0] != (char)0x89) {
        cout << "Error: invalid header! Input file is corrupt or not a valid SCI patch resource" << endl;
        exit(EXIT_FAILURE);
    }

    // Check for title string length in second byte of header to use as offset for future file handling
    patfile.seekg(0x01);
    char titleStringSize[1];
    patfile.read(titleStringSize, 1);
    streamoff titleOffset = static_cast<streamoff>(static_cast<int>(titleStringSize[0]));

    // Check size of file to ensure it's valid
    patfile.seekg(0, ios::end);
    streamoff length = patfile.tellg();
    patfile.seekg(0, ios::beg);

    if (length != 6148 + titleOffset && length != 3074 + titleOffset) {
        cout << patfile_name << " is not the expected size (3074 or 6148 bytes + title string length). Not a valid FB-01 SCI0 Patch file." 
            << endl << "Actual size: " << length << endl << "Title string length: " << static_cast<int>(titleStringSize[0]) << endl;
        return 1;
    }

    //
    // Determine if patfile has one or two banks and set nVoices appropriately for later loop iteration
    //

    int nVoices = 0;
    vector<char> data;

    //
    // Patfile contains two banks (96 voices)
    //
    if (length == 6148 + titleOffset) {
        nVoices = 96;
        // Ensure the ABCDh bytes exist at address 0xC02 between the two banks
        // (offset by the title string length from the file header)
        patfile.seekg(0xC02 + titleOffset);
        patfile.read(buffer, 2);
        if (buffer[0] != (char)0xAB && buffer[1] != (char)0xCD) {
            cout << "Error: bank separator bytes missing! Input file is not a valid FB-01 SCI patch resource." << endl;
            return 1;
        }
        // Read the input patch file into memory, then close the input file
        read_file(patfile, data, titleOffset, nVoices);
        patfile.close();

        // Prepare two output sysex bank filenames
        char output_bank1[256];
        char output_bank2[256];
        strcpy(output_bank1, output_bank);
        strcat(output_bank1, "_a.syx");
        strcpy(output_bank2, output_bank);
        strcat(output_bank2, "_b.syx");
        // Check if output bank files 1 and 2 already exist. If they do, ask user whether to overwrite or abort
        overwrite_check(output_bank1);
        overwrite_check(output_bank2);

        // Split the bytes of each instrument voice packet in order of: low nibble = high byte, high nibble = low byte
        vector<char> splitData1;
        vector<char> splitData2;
        splitData1.reserve(data.size());
        splitData2.reserve(data.size());
        splitData1.clear();
        splitData2.clear();

        nibblize_data(data, splitData1, &splitData2);

        // Create the sysex bank files with the new "nibblized" data
        write_to_file(splitData1, output_bank1, &splitData2, output_bank2);

        cout << "Two FB-01 sysex banks successfully created!" << endl;
    }

    //
    // Patfile contains only one bank (48 voices)
    //
    else if (length == 3074 + titleOffset) {
        nVoices = 48;
        // Read the input patch file into memory, then close the input file
        read_file(patfile, data, titleOffset, nVoices);
        patfile.close();

        // Prepare single output sysex bank filename
        strcat(output_bank, ".syx");
        overwrite_check(output_bank);

        // Split the bytes of each instrument voice packet in order of: low nibble = high byte, high nibble = low byte
        vector<char> splitData1;
        splitData1.reserve(data.size());
        splitData1.clear();

        nibblize_data(data, splitData1);

        // Create the single sysex bank file with the new "nibblized" data
        write_to_file(splitData1, output_bank);

        cout << "FB-01 sysex bank successfully created!" << endl;
    }

    return 0;
}

void read_file(ifstream& file, vector<char>& data, streamoff titleOffset, int nVoices) {
    // Read the file starting at the first voice data byte after the header bytes. Iterate through each
    // byte for each of the 96 voices. (we must skip the separator bytes ABCDh on voice 49, which is
    //      voice 1 of bank B)
    streampos pos = 0x02 + titleOffset;

    for (int i = 0; i < nVoices; i++) {
        // If patch file has 2 banks and we've reached the end of bank 1, skip the bank separator bytes ABCDh
        if (nVoices == 96 && i == 48) pos += 2;
        
        // Store the instrument voice data into "data"
        file.seekg(pos);
        char buffer[64];
        file.read(buffer, 64);
        data.insert(data.end(), buffer, buffer + 64);
        pos += 64;
    }
}

void nibblize_data(vector<char>& data, vector<char>& splitData1, vector<char>* splitData2) {
    // Check to ensure that both sets of instrument voice packets equal 6144 bytes in length (64 bytes per 96 voices form the patch file).
    if (data.size() != 6144 && data.size() != 3072) {
        cout << "Error: data vector not the expected size (6144 or 3072)" << endl;
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
    if (splitData2) {
        for (int i = 0; i < 48; i++) {
            // set packet size of nibblized bytes preceding the packet:
            //          0x01 0x00 = 128 (%0000000h, %0lllllll -> %00000000, %hlllllll)
            (*splitData2).push_back(0x01);
            (*splitData2).push_back(0x00);

            // 64 bytes per packet
            for (int j = 0; j < 64; j++) {
                int index = (48 + i) * 64 + j;
                (*splitData2).push_back(data[index] & 0x0F);
                (*splitData2).push_back((data[index] >> 4) & 0x0F);
            }

            // calculate checksum with 2's complement of sum
            unsigned char checksum = 0;
            for (int j = 2; j < 130; j++) {
                checksum += static_cast<unsigned int>((*splitData2)[i * 131 + j]);
            }
            checksum = ((~(checksum & 0xFF)) + 1) & 0x7F;
            (*splitData2).push_back(checksum);
        }
    }
}

void write_to_file(vector<char> splitData1, const char* output_bank1, vector<char>* splitData2, const char* output_bank2) {
    // Open the output file in binary mode for writing
    ofstream out_file1(output_bank1, ios::binary);
    
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
    // 
    // WHEN TWO BANKS ARE BEING GENERATED:
    // Only the first 7 char's of outfile1 are pulled from the filename. If it is less than 7 char's already,
    // it fills the remaining spaces with 0x20 (space character) and the 8th character with a '1' (for bank 1).
    //
    // WHEN ONE BANK IS BEING GENERATED:
    // Pull the first 8 char's of outfile1 from the filename. Again, if it is less than 8 char's already,
    // fill the remaining spaces with 0x20 (space character).

    char bank1InfoPacket[32] = { 0 };
    char* bank1name = bank1InfoPacket;

    int len = strlen(output_bank1);
    
    // If two banks, make label 7 char's long to make room in label for "1" (part 1), else make it full 8 char's
    int bank_name_maxlen = (splitData2) ? 7 : 8; 
    
    strncpy(bank1name, output_bank1, bank_name_maxlen); // Copy up to 7 (or 8) characters from output_bank1 into bank1name
    // Convert the bank name to uppercase
    for (int i = 0; i < bank_name_maxlen; i++) {
        bank1name[i] = toupper(bank1name[i]);
    }
    // If the length of output_bank1 is less than 7 (or 8), fill the remaining characters with spaces
    if (len < bank_name_maxlen) {
        memset(bank1name + len, 0x20, bank_name_maxlen - len);
    }
    // If two banks, set the 8th character to '1', signifying "part 1" in label
    if (splitData2) bank1name[7] = '1';

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
    
    // Begin writing bank A sysex file
    out_file1.seekp(0, ios::beg);
    out_file1.write(reinterpret_cast<char*>(&bank1header), sizeof(bank1header)); // Write the header to outfile1
    out_file1.write(splitData1.data(), splitData1.size()); // Write the already nibblized and checksummed voice data packets
    out_file1.write("\xF7", 1); // Write the final closing byte to end the exclusive message
    out_file1.close();


    //
    // Now for bank 2's header and info packet (if being processed)
    //
    if (splitData2) {
        ofstream out_file2(output_bank2, ios::binary);

        char bank2header[74] = { '\xF0', '\x43', '\x75', '\x00', '\x00', '\x00', '\x01', '\x00', '\x40', '\0' };
        char bank2InfoPacket[32] = { 0 };
        char* bank2name = bank2InfoPacket;

        len = strlen(output_bank2);
        strncpy(bank2name, output_bank2, 7); // Copy up to 7 characters from output_bank2 into bank2name
        // Convert the bank name to uppercase
        for (int i = 0; i < 7; i++) {
            bank2name[i] = toupper(bank2name[i]);
        }
        // If the length of output_bank2 is less than 7, fill the remaining characters with spaces
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
   
        // Begin writing bank B sysex file
        out_file2.seekp(0, ios::beg);
        out_file2.write(reinterpret_cast<char*>(&bank2header), sizeof(bank2header));
        out_file2.write((*splitData2).data(), (*splitData2).size());
        out_file2.write("\xF7", 1);
        out_file2.close();
    }
}

bool check_file_exists(const char* filename) {
    ifstream infile(filename);
    return infile.good();
}

void overwrite_check(string output_filename) {
    ifstream file(output_filename);
    if (file.good()) {
        cout << "\"" << output_filename << "\" already exists. Do you want to overwrite it? (Y/N): ";
        string answer;
        cin >> answer;
        if (answer == "Y" || answer == "y") {
            ofstream file(output_filename, ios::trunc);
            cout << "OVERWRITTEN!" << endl << endl;
            file.close();
        }
        else {
            cout << "Aborting..." << endl;
            exit(EXIT_FAILURE);
        }
    }
}
