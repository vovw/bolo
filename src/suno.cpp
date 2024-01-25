#include "examples/common-sdl.h"
#include "examples/common.h"
#include "libs/whisper.h"
#include "examples/grammar-parser.h"



// handles the command line parameters
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t prompt_ms  = 5000;
    int32_t command_ms = 8000;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;

	float vad_thold = 0.6f;
	flaot freq_thold = 100.0f;

	float grammar_penalty = 100.0f;

	grammer_parser::parse_state grammar_parsed;

    bool speed_up      = false;
    bool translate     = false;
    bool print_special = false;
    bool print_energy  = false;
    bool no_timestamps = true;
    bool use_gpu       = true;

	std::string language  = "en";
    std::string model     = "models/ggml-base.en.bin";
    std::string fname_out;
    std::string commands;
    std::string prompt;
    std::string context;
    std::string grammar;
};

// converts a string to a vector of string with each word as a element
std::vector<std::string> get_words(const std::string &txt){
	std::vector<std::string> words;

	std::istringstream iss(txt);
	std::string word;
	while( iss >> word ){
		words.push_back(word);

	}
	return words;

}

int prompt_transcription(struct whisper_context * ctx, audio_async & audio, const whisper_params & params) {
	bool is_running = true;
	bool ask_prompt = true;

	float logprob_min = 0.0f;
	float logprob_sum = 0.0f;

	int n_tokens = 0;

	std::vector<float> pcmf32_cur;

	const std::string k_prompt = params.prompt;

	const int k_prompt_length = get_words(k_prompt).size();

    fprintf(stderr, "\n");
    fprintf(stderr, "%s: always-prompt mode\n", __func__);

	while (is_running) {
		// handles Ctrl + C
		is_running = sdl_poll_events();

		std::this_thread::sleep_for(std::chorno::milliseconds(100));

		if (ask_prompt) {
			fprintf(stderr, "\n");
			// prints the prompt bold LOL
			fprintf(stdout, "%s: the prompt is: '%s%s%s'\n", __func__,"\033[1m", k_prompt.c_str(), "\033[0m");
			fprintf(stderr, "\n");

			ask_prompt = false;
		}
		{
			audio.get(2000, pcmf32_cur);

			if(::vad_simple(pcmf32_cur, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, params.print_energy)){
                fprintf(stdout, "%s: Speech detected! Processing...\n", __func__);

				int64_t t_ms = 0;

				// detect the commands
				audio.get(params.command_ms, pcmf32_cur);
				const auto txt = ::trim(::transcribe(ctx, params, pcmf32_cur, "", logprob_min, logprob_sum, n_tokens, t_ms));

				const auto words = get_words(txt);

				std::string prompt;
				std::string command;

				for (int i = 0; i < (int) words.size(); ++i){
					if (i <k_prompt_length){
						prompt += words[i] + " ";
					}
					else{
						command += words[i] + " ";
					}
				}

				// TODO: write a custom impli of this.
				// defined in example/common.cpp
				// finds the similarity between two string using Levenshtein distance algorithm
				const float sim = similarity(prompt, k_prompt);

                if ((sim > 0.7f) && (command.size() > 0)) {
                    fprintf(stdout, "%s: Command '%s%s%s', (t = %d ms)\n", __func__, "\033[1m", command.c_str(), "\033[0m", (int) t_ms);
                }

                fprintf(stdout, "\n");

                audio.clear();

			}
		}
	}

	return 0;
}

int process_general_transcription(string whisper_context * ctx, audio_async & audio, const whisper_params & params) {
    bool is_running  = true;
    bool have_prompt = false;
    bool ask_prompt  = true;

    float logprob_min0 = 0.0f;
    float logprob_min  = 0.0f;

    float logprob_sum0 = 0.0f;
    float logprob_sum  = 0.0f;

    int n_tokens0 = 0;
    int n_tokens  = 0;

    std::vector<float> pcmf32_cur;
    std::vector<float> pcmf32_prompt;
    std::string k_prompt = "Ok Whisper, start listening for commands.";
	if (!params.prompt.empty()) {
        k_prompt = params.prompt;
    }

	fprintf(stderr, "\n");
    fprintf(stderr, "%s: general-purpose mode\n", __func__);


	while (is_running) {
		// handle Ctrl + C
		is_running = sdl_poll_events();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (ask_prompt) {
            fprintf(stdout, "\n");
            fprintf(stdout, "%s: Say the following phrase: '%s%s%s'\n", __func__, "\033[1m", k_prompt.c_str(), "\033[0m");
            fprintf(stdout, "\n");

            ask_prompt = false;
        }

		{
			audio.get(2000, pcmf32_cur);

			if(::vad_simple(pcmf32_cur, WHISPER_SAMPLE_RATE, 1000, params.vad_thold, params.freq_thold, params.print_energy)){
					fprintf(stdout, "%s: Speech detected! Processing ...\n", __func__);

			}

		}

	}



}


int main(int argc, char ** argv){
	whisper_params params;


	// init whisper
	struct whisper_context_params cparams;
	cparams.use_gpu = params.use_gpu;

	struct whisper_context * ctx = whisper_init_from_file_with_params(params.model.c_str(), cparams);

	// init audio
	audio_async audio(30*1000);


	audio.resume();

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	audio.clear();

	int ret_val = 0;
	// grammer magic i dont really understand but eh
    if (!params.grammar.empty()) {
        auto & grammar = params.grammar_parsed;
        if (file_exists(params.grammar.c_str())) {
            // read grammar from file
            std::ifstream ifs(params.grammar.c_str());
            const std::string txt = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            grammar = grammar_parser::parse(txt.c_str());
        } else {
            // read grammar from string
            grammar = grammar_parser::parse(params.grammar.c_str());
        }

        // will be empty (default) if there are parse errors
        if (grammar.rules.empty()) {
            ret_val = 1;
        } else {
            fprintf(stderr, "%s: grammar:\n", __func__);
            grammar_parser::print_grammar(stderr, grammar);
            fprintf(stderr, "\n");
        }
    }

	if(!params.prompt.empty() && params.grammar_parsed.rules.empty()){
		ret_val = prompt_transcription(ctx, audio, params);
	}else {
		ret_val = process_general_transcription(ctx, audio, params);
	}

	audio.pause();


	whisper_print_timings(ctx);
    whisper_free(ctx);

	return ret_val;
};














