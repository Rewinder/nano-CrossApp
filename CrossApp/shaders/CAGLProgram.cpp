

#include "basics/CAApplication.h"
#include "CAGLProgram.h"
#include "ccGLStateCache.h"
#include "ccMacros.h"
#include "platform/CAFileUtils.h"*
#include "support/data_support/uthash.h"
// extern
#include "kazmath/GL/matrix.h"
#include "kazmath/kazmath.h"

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
#include "CAGPUAdapter.h"
#endif

#define GLSL_VERSION_CODE_LEN 64

NS_CC_BEGIN

typedef struct _hashUniformEntry
{
    GLvoid*         value;       // value
    unsigned int    location;    // Key
    UT_hash_handle  hh;          // hash entry
} tHashUniformEntry;

CAGLProgram::CAGLProgram()
: m_uProgram(0)
, m_uVertShader(0)
, m_uFragShader(0)
, m_pHashForUniforms(NULL)
, m_bUsesTime(false)
, m_hasShaderCompiler(true)
{
    memset(m_uUniforms, 0, sizeof(m_uUniforms));
}

CAGLProgram::~CAGLProgram()
{
    CCLOGINFO("CrossApp: %s %d deallocing 0x%X", __FUNCTION__, __LINE__, this);

    // there is no need to delete the shaders. They should have been already deleted.
    CCAssert(m_uVertShader == 0, "Vertex Shaders should have been already deleted");
    CCAssert(m_uFragShader == 0, "Fragment Shaders should have been already deleted");

    if (m_uProgram) 
    {
        ccGLDeleteProgram(m_uProgram);
    }

    tHashUniformEntry *current_element, *tmp;

    // Purge uniform hash
    HASH_ITER(hh, m_pHashForUniforms, current_element, tmp)
    {
        HASH_DEL(m_pHashForUniforms, current_element);
        free(current_element->value);
        free(current_element);
    }
}

bool CAGLProgram::initWithVertexShaderByteArray(const GLchar* vShaderByteArray, const GLchar* fShaderByteArray)
{
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8)
    GLboolean hasCompiler = false;
    glGetBooleanv(GL_SHADER_COMPILER, &hasCompiler);
    m_hasShaderCompiler = (hasCompiler == GL_TRUE);

    if(!m_hasShaderCompiler)
    {
        return initWithPrecompiledProgramByteArray(vShaderByteArray,fShaderByteArray);
    }
#endif

    m_uProgram = glCreateProgram();
    CHECK_GL_ERROR_DEBUG();

    m_uVertShader = m_uFragShader = 0;

    if (vShaderByteArray)
    {
        if (!compileShader(&m_uVertShader, GL_VERTEX_SHADER, vShaderByteArray))
        {
            CCLOG("CrossApp: ERROR: Failed to compile vertex shader");
 			return false;
       }
    }

    // Create and compile fragment shader
    if (fShaderByteArray)
    {
        if (!compileShader(&m_uFragShader, GL_FRAGMENT_SHADER, fShaderByteArray))
        {
            CCLOG("CrossApp: ERROR: Failed to compile fragment shader");
			return false;
        }
    }

    if (m_uVertShader)
    {
        glAttachShader(m_uProgram, m_uVertShader);
    }
    CHECK_GL_ERROR_DEBUG();

    if (m_uFragShader)
    {
        glAttachShader(m_uProgram, m_uFragShader);
    }
    m_pHashForUniforms = NULL;
    
    CHECK_GL_ERROR_DEBUG();

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    m_shaderId = CCPrecompiledShaders::sharedPrecompiledShaders()->addShaders(vShaderByteArray, fShaderByteArray);
#endif

    return true;
}

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8)
bool CAGLProgram::initWithPrecompiledProgramByteArray(const GLchar* vShaderByteArray, const GLchar* fShaderByteArray)
{
    bool haveProgram = false;

    m_uProgram = glCreateProgram();
    CHECK_GL_ERROR_DEBUG();

    m_uVertShader = m_uFragShader = 0;

    haveProgram = CCPrecompiledShaders::sharedPrecompiledShaders()->loadProgram(m_uProgram, vShaderByteArray, fShaderByteArray);

    CHECK_GL_ERROR_DEBUG();
    m_pHashForUniforms = NULL;

    CHECK_GL_ERROR_DEBUG();  

    return haveProgram;
}
#endif

bool CAGLProgram::initWithVertexShaderFilename(const char* vShaderFilename, const char* fShaderFilename)
{
    const GLchar * vertexSource = (GLchar*) FileUtils::getInstance()->getFileString(vShaderFilename).c_str();
    const GLchar * fragmentSource = (GLchar*) FileUtils::getInstance()->getFileString(fShaderFilename).c_str();

    return initWithVertexShaderByteArray(vertexSource, fragmentSource);
}

const char* CAGLProgram::description()
{
    return crossapp_format_string("<CAGLProgram = "
                                  CC_FORMAT_PRINTF_SIZE_T
                                  " | Program = %i, VertexShader = %i, FragmentShader = %i>",
                                  (size_t)this, m_uProgram, m_uVertShader, m_uFragShader).c_str();
}

bool CAGLProgram::compileShader(GLuint * shader, GLenum type, const GLchar* source)
{
    GLint status;
 
    if (!source)
    {
        return false;
    }
    
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
	std::string szGLSLVer = CAGPUAdapter::create()->getGLSLGenerationString();
#endif

    const GLchar *sources[] = {
#if (CC_TARGET_PLATFORM != CC_PLATFORM_WIN32 && CC_TARGET_PLATFORM != CC_PLATFORM_LINUX && CC_TARGET_PLATFORM != CC_PLATFORM_MAC)
        (type == GL_VERTEX_SHADER ? "precision highp float;\n" : "precision mediump float;\n"),
#endif

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
		szGLSLVer.c_str(),
#endif
		
        "uniform mat4 CC_PMatrix;\n"
        "uniform mat4 CC_MVMatrix;\n"
        "uniform mat4 CC_MVPMatrix;\n"
        "uniform vec4 CC_Time;\n"
        "uniform vec4 CC_SinTime;\n"
        "uniform vec4 CC_CosTime;\n"
        "uniform vec4 CC_Random01;\n"
        "//CC INCLUDES END\n\n",
        source,
    };

    *shader = glCreateShader(type);
    glShaderSource(*shader, sizeof(sources)/sizeof(*sources), sources, NULL);
    glCompileShader(*shader);

    glGetShaderiv(*shader, GL_COMPILE_STATUS, &status);

    if (! status)
    {
        GLsizei length;
		glGetShaderiv(*shader, GL_SHADER_SOURCE_LENGTH, &length);
		GLchar* src = (GLchar *)malloc(sizeof(GLchar) * length);
		
		glGetShaderSource(*shader, length, NULL, src);
		CCLOG("CrossApp: ERROR: Failed to compile shader:\n%s", src);
        
        if (type == GL_VERTEX_SHADER)
        {
            CCLOG("CrossApp: %s", vertexShaderLog());
        }
        else
        {
            CCLOG("CrossApp: %s", fragmentShaderLog());
        }
        free(src);

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
        return false;
#else
		abort();
#endif
    }
    return (status == GL_TRUE);
}

void CAGLProgram::addAttribute(const char* attributeName, GLuint index)
{
    glBindAttribLocation(m_uProgram, index, attributeName);
}

void CAGLProgram::updateUniforms()
{
    m_uUniforms[kCCUniformPMatrix] = glGetUniformLocation(m_uProgram, kCCUniformPMatrix_s);
	m_uUniforms[kCCUniformMVMatrix] = glGetUniformLocation(m_uProgram, kCCUniformMVMatrix_s);
	m_uUniforms[kCCUniformMVPMatrix] = glGetUniformLocation(m_uProgram, kCCUniformMVPMatrix_s);
	
	m_uUniforms[kCCUniformTime] = glGetUniformLocation(m_uProgram, kCCUniformTime_s);
	m_uUniforms[kCCUniformSinTime] = glGetUniformLocation(m_uProgram, kCCUniformSinTime_s);
	m_uUniforms[kCCUniformCosTime] = glGetUniformLocation(m_uProgram, kCCUniformCosTime_s);
	
	m_bUsesTime = (
                 m_uUniforms[kCCUniformTime] != -1 ||
                 m_uUniforms[kCCUniformSinTime] != -1 ||
                 m_uUniforms[kCCUniformCosTime] != -1
                 );
    
	m_uUniforms[kCCUniformRandom01] = glGetUniformLocation(m_uProgram, kCCUniformRandom01_s);

    m_uUniforms[kCCUniformSampler] = glGetUniformLocation(m_uProgram, kCCUniformSampler_s);

    this->use();
    
    // Since sample most probably won't change, set it to 0 now.
    this->setUniformLocationWith1i(m_uUniforms[kCCUniformSampler], 0);
}

bool CAGLProgram::link()
{
    CCAssert(m_uProgram != 0, "Cannot link invalid program");
 
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8)
    if(!m_hasShaderCompiler)
    {
        // precompiled shader program is already linked
        return true;
    }
#endif

    GLint status = GL_TRUE;
    
    glLinkProgram(m_uProgram);

    if (m_uVertShader)
    {
        glDeleteShader(m_uVertShader);
    }
    
    if (m_uFragShader)
    {
        glDeleteShader(m_uFragShader);
    }
    
    m_uVertShader = m_uFragShader = 0;
	
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8)
    glGetProgramiv(m_uProgram, GL_LINK_STATUS, &status);
	
    if (status == GL_FALSE)
    {
        CCLOG("CrossApp: ERROR: Failed to link program: %i", m_uProgram);
        ccGLDeleteProgram(m_uProgram);
        m_uProgram = 0;
    }
#endif

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    if (status == GL_TRUE)
    {
        CCPrecompiledShaders::sharedPrecompiledShaders()->addProgram(m_uProgram, m_shaderId);
    }
#endif


    return (status == GL_TRUE);
}

void CAGLProgram::use()
{
    ccGLUseProgram(m_uProgram);
}

const char* CAGLProgram::logForOpenGLObject(GLuint object, GLInfoFunction infoFunc, GLLogFunction logFunc)
{
    GLint logLength = 0, charsWritten = 0;

    infoFunc(object, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength < 1)
        return 0;

    char *logBytes = (char*)malloc(logLength);
    logFunc(object, logLength, &charsWritten, logBytes);

	static std::string szlog;
	szlog = logBytes;
    free(logBytes);
	return szlog.c_str();
}

const char* CAGLProgram::vertexShaderLog()
{
    return this->logForOpenGLObject(m_uVertShader, (GLInfoFunction)&glGetShaderiv, (GLLogFunction)&glGetShaderInfoLog);
}

const char* CAGLProgram::fragmentShaderLog()
{
    return this->logForOpenGLObject(m_uFragShader, (GLInfoFunction)&glGetShaderiv, (GLLogFunction)&glGetShaderInfoLog);
}

const char* CAGLProgram::programLog()
{
    return this->logForOpenGLObject(m_uProgram, (GLInfoFunction)&glGetProgramiv, (GLLogFunction)&glGetProgramInfoLog);
}

// Uniform cache

bool CAGLProgram::updateUniformLocation(GLint location, GLvoid* data, unsigned int bytes)
{
    if (location < 0)
    {
        return false;
    }
    
    bool updated = true;
    tHashUniformEntry *element = NULL;
    HASH_FIND_INT(m_pHashForUniforms, &location, element);

    if (! element)
    {
        element = (tHashUniformEntry*)malloc( sizeof(*element) );

        // key
        element->location = location;

        // value
        element->value = malloc( bytes );
        memcpy(element->value, data, bytes );

        HASH_ADD_INT(m_pHashForUniforms, location, element);
    }
    else
    {
        if (memcmp(element->value, data, bytes) == 0)
        {
            updated = false;
        }
        else
        {
            memcpy(element->value, data, bytes);
        }
    }

    return updated;
}

GLint CAGLProgram::getUniformLocationForName(const char* name)
{
    CCAssert(name != NULL, "Invalid uniform name" );
    CCAssert(m_uProgram != 0, "Invalid operation. Cannot get uniform location when program is not initialized");
    
    return glGetUniformLocation(m_uProgram, name);
}

void CAGLProgram::setUniformLocationWith1i(GLint location, GLint i1)
{
    bool updated =  updateUniformLocation(location, &i1, sizeof(i1)*1);
    
    if( updated )
    {
        glUniform1i( (GLint)location, i1);
    }
}

void CAGLProgram::setUniformLocationWith2i(GLint location, GLint i1, GLint i2)
{
    GLint ints[2] = {i1,i2};
    bool updated =  updateUniformLocation(location, ints, sizeof(ints));
    
    if( updated )
    {
        glUniform2i( (GLint)location, i1, i2);
    }
}

void CAGLProgram::setUniformLocationWith3i(GLint location, GLint i1, GLint i2, GLint i3)
{
    GLint ints[3] = {i1,i2,i3};
    bool updated =  updateUniformLocation(location, ints, sizeof(ints));
    
    if( updated )
    {
        glUniform3i( (GLint)location, i1, i2, i3);
    }
}

void CAGLProgram::setUniformLocationWith4i(GLint location, GLint i1, GLint i2, GLint i3, GLint i4)
{
    GLint ints[4] = {i1,i2,i3,i4};
    bool updated =  updateUniformLocation(location, ints, sizeof(ints));
    
    if( updated )
    {
        glUniform4i( (GLint)location, i1, i2, i3, i4);
    }
}

void CAGLProgram::setUniformLocationWith2iv(GLint location, GLint* ints, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, ints, sizeof(int)*2*numberOfArrays);
    
    if( updated )
    {
        glUniform2iv( (GLint)location, (GLsizei)numberOfArrays, ints );
    }
}

void CAGLProgram::setUniformLocationWith3iv(GLint location, GLint* ints, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, ints, sizeof(int)*3*numberOfArrays);
    
    if( updated )
    {
        glUniform3iv( (GLint)location, (GLsizei)numberOfArrays, ints );
    }
}

void CAGLProgram::setUniformLocationWith4iv(GLint location, GLint* ints, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, ints, sizeof(int)*4*numberOfArrays);
    
    if( updated )
    {
        glUniform4iv( (GLint)location, (GLsizei)numberOfArrays, ints );
    }
}

void CAGLProgram::setUniformLocationWith1f(GLint location, GLfloat f1)
{
    bool updated =  updateUniformLocation(location, &f1, sizeof(f1)*1);

    if( updated )
    {
        glUniform1f( (GLint)location, f1);
    }
}

void CAGLProgram::setUniformLocationWith2f(GLint location, GLfloat f1, GLfloat f2)
{
    GLfloat floats[2] = {f1,f2};
    bool updated =  updateUniformLocation(location, floats, sizeof(floats));

    if( updated )
    {
        glUniform2f( (GLint)location, f1, f2);
    }
}

void CAGLProgram::setUniformLocationWith3f(GLint location, GLfloat f1, GLfloat f2, GLfloat f3)
{
    GLfloat floats[3] = {f1,f2,f3};
    bool updated =  updateUniformLocation(location, floats, sizeof(floats));

    if( updated )
    {
        glUniform3f( (GLint)location, f1, f2, f3);
    }
}

void CAGLProgram::setUniformLocationWith4f(GLint location, GLfloat f1, GLfloat f2, GLfloat f3, GLfloat f4)
{
    GLfloat floats[4] = {f1,f2,f3,f4};
    bool updated =  updateUniformLocation(location, floats, sizeof(floats));

    if( updated )
    {
        glUniform4f( (GLint)location, f1, f2, f3,f4);
    }
}

void CAGLProgram::setUniformLocationWith2fv(GLint location, GLfloat* floats, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, floats, sizeof(float)*2*numberOfArrays);

    if( updated )
    {
        glUniform2fv( (GLint)location, (GLsizei)numberOfArrays, floats );
    }
}

void CAGLProgram::setUniformLocationWith3fv(GLint location, GLfloat* floats, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, floats, sizeof(float)*3*numberOfArrays);

    if( updated )
    {
        glUniform3fv( (GLint)location, (GLsizei)numberOfArrays, floats );
    }
}

void CAGLProgram::setUniformLocationWith4fv(GLint location, GLfloat* floats, unsigned int numberOfArrays)
{
    bool updated =  updateUniformLocation(location, floats, sizeof(float)*4*numberOfArrays);

    if( updated )
    {
        glUniform4fv( (GLint)location, (GLsizei)numberOfArrays, floats );
    }
}


void CAGLProgram::setUniformLocationWithMatrix4fv(GLint location, GLfloat* matrixArray, unsigned int numberOfMatrices)
{
    bool updated =  updateUniformLocation(location, matrixArray, sizeof(float)*16*numberOfMatrices);

    if( updated )
    {
        glUniformMatrix4fv( (GLint)location, (GLsizei)numberOfMatrices, GL_FALSE, matrixArray);
    }
}

void CAGLProgram::setUniformsForBuiltins()
{
    kmMat4 matrixP;
	kmMat4 matrixMV;
	kmMat4 matrixMVP;
	
	kmGLGetMatrix(KM_GL_PROJECTION, &matrixP);
	kmGLGetMatrix(KM_GL_MODELVIEW, &matrixMV);
	
	kmMat4Multiply(&matrixMVP, &matrixP, &matrixMV);
    
    setUniformLocationWithMatrix4fv(m_uUniforms[kCCUniformPMatrix], matrixP.mat, 1);
    setUniformLocationWithMatrix4fv(m_uUniforms[kCCUniformMVMatrix], matrixMV.mat, 1);
    setUniformLocationWithMatrix4fv(m_uUniforms[kCCUniformMVPMatrix], matrixMVP.mat, 1);
	
	if(m_bUsesTime)
    {
		CAApplication *director = CAApplication::getApplication();
		// This doesn't give the most accurate global time value.
		// CrossApp doesn't store a high precision time value, so this will have to do.
		// Getting Mach time per frame per shader using time could be extremely expensive.
        float time = director->getTotalFrames() * director->getAnimationInterval();
		
        setUniformLocationWith4f(m_uUniforms[kCCUniformTime], time/10.0, time, time*2, time*4);
        setUniformLocationWith4f(m_uUniforms[kCCUniformSinTime], time/8.0, time/4.0, time/2.0, sinf(time));
        setUniformLocationWith4f(m_uUniforms[kCCUniformCosTime], time/8.0, time/4.0, time/2.0, cosf(time));
	}
	
	if (m_uUniforms[kCCUniformRandom01] != -1)
    {
        setUniformLocationWith4f(m_uUniforms[kCCUniformRandom01], CCRANDOM_0_1(), CCRANDOM_0_1(), CCRANDOM_0_1(), CCRANDOM_0_1());
	}
}

void CAGLProgram::reset()
{
    m_uVertShader = m_uFragShader = 0;
    memset(m_uUniforms, 0, sizeof(m_uUniforms));
    

    // it is already deallocated by android
    //ccGLDeleteProgram(m_uProgram);
    m_uProgram = 0;

    
    tHashUniformEntry *current_element, *tmp;
    
    // Purge uniform hash
    HASH_ITER(hh, m_pHashForUniforms, current_element, tmp) 
    {
        HASH_DEL(m_pHashForUniforms, current_element);
        free(current_element->value);
        free(current_element);
    }
    m_pHashForUniforms = NULL;
}

NS_CC_END
