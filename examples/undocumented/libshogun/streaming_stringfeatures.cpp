// This example simply demonstrates the use/working of StreamingStringFeatures

#include <shogun/lib/common.h>
#include <shogun/lib/config.h>
#include <shogun/lib/SGString.h>
#include <shogun/base/init.h>

#include <shogun/io/streaming/StreamingAsciiFile.h>
#include <shogun/features/streaming/StreamingStringFeatures.h>

using namespace shogun;

void display_vector(const SGString<char> &vec)
{
	printf("\nNew Vector\n------------------\n");
	printf("Length=%d.\n", vec.slen);
	for (int32_t i=0; i<vec.slen; i++)
	{
		printf("%c", vec.string[i]);
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	init_shogun_with_defaults();

	// Create a StreamingAsciiFile from our input file
	CStreamingAsciiFile* file = new CStreamingAsciiFile("../data/fm_train_dna.dat");
	SG_REF(file);

	// This file contains unlabelled data, so the second arg is `false'.
	CStreamingStringFeatures<char>* feat = new CStreamingStringFeatures<char>(file, false, 1024);
	SG_REF(feat);
	// Alphabet to use is DNA
	feat->use_alphabet(DNA);

	// Loop over all examples and simply display each example
	feat->start_parser();
	while (feat->get_next_example())
	{
		SGString<char> vec = feat->get_vector();
		display_vector(vec);
		feat->release_example();
	}
	feat->end_parser();

	// Get the alphabet and display the histogram
	CAlphabet* alpha = feat->get_alphabet();
	printf("\nThe histogram is:\n");
	alpha->print_histogram();
	SG_UNREF(alpha);

	SG_UNREF(feat);

	exit_shogun();
	return 0;
}
